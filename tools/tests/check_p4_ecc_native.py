"""Run production software ECC and the upstream HW branch against OpenSSL vectors.

The production test uses no modeled accelerator. A separate regression test models
the peripheral/locks to check the upstream port's conversions and scalar guard.
GNU compiler barriers are replaced only for this MSVC-native test translation.
"""
import hashlib
import os
from pathlib import Path
import subprocess

from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat

ROOT = Path(__file__).resolve().parents[2]
P4 = ROOT / "tools/boards/elecrow_p4_5inch"
OUT = ROOT / "tools/tests/build-checks/p4-ecc-native"
SDK = Path(os.environ["LOCALAPPDATA"]) / "Arduino15/packages/esp32/tools/esp32p4-libs/3.3.12"


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    order = int("ffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551", 16)

    def blob(data):
        return "{" + ",".join(str(b) for b in data) + "}"

    rows = []
    for i in range(32):
        a = i + 2 if i < 8 else int.from_bytes(hashlib.sha256(f"OFE ECC A {i}".encode()).digest(), "big") % (order-1) + 1
        b = int.from_bytes(hashlib.sha256(f"OFE ECC B {i}".encode()).digest(), "big") % (order-1) + 1
        ka, kb = (ec.derive_private_key(n, ec.SECP256R1()) for n in (a, b))
        pa, pb = (k.public_key().public_bytes(Encoding.X962, PublicFormat.UncompressedPoint)[1:] for k in (ka, kb))
        rows.append("{" + ",".join(blob(x) for x in (a.to_bytes(32,"big"), pa, pb, ka.exchange(ec.ECDH(), kb.public_key()))) + "}")
    (OUT / "ecc_vectors.h").write_text("static const struct { unsigned char private_a[32],public_a[64],public_b[64],shared[32]; } vectors[]={\n" + ",\n".join(rows) + "\n};\n")
    hw = r"""#pragma once
#include <stdbool.h>
#include <stdint.h>
#define SOC_ECC_SUPPORTED 1
#define ECC_RCC_ATOMIC() for(int once=1;once;once=0)
typedef enum { ECC_MODE_VERIFY, ECC_MODE_POINT_MUL, ECC_MODE_VERIFY_THEN_POINT_MUL } ecc_mode_t;
void esp_crypto_ecc_lock_acquire(void);
void esp_crypto_ecc_lock_release(void);
void ecc_ll_enable_bus_clock(bool);
void ecc_ll_power_up(void);
void ecc_ll_power_down(void);
void ecc_ll_reset_register(void);
void ecc_hal_write_verify_param(const uint8_t*,const uint8_t*,uint8_t);
void ecc_hal_set_mode(ecc_mode_t);
void ecc_hal_start_calc(void);
bool ecc_hal_is_calc_finished(void);
int ecc_hal_read_verify_result(void);
void ecc_hal_write_mul_param(const uint8_t*,const uint8_t*,const uint8_t*,uint8_t);
void ecc_hal_enable_constant_time_point_mul(bool);
int ecc_hal_read_mul_result(uint8_t*,uint8_t*,uint8_t);
"""
    (OUT / "hw.h").write_text(hw)
    (OUT / "esp_random.h").write_text('#pragma once\n#include <stddef.h>\nvoid esp_fill_random(void*,size_t);\n')
    for name in ("soc/soc_caps.h", "esp_crypto_lock.h", "esp_private/esp_crypto_lock_internal.h", "hal/ecc_hal.h", "hal/ecc_ll.h"):
        path = OUT / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('#include "hw.h"\n')
    source = '#include <assert.h>\n#include "hw.h"\n#define __asm__\n#define __volatile__(...) ((void)0)\n'
    policy = (P4 / "OfeP4BleEccSoftware.h").read_text()
    source += '#ifndef OFE_TEST_HW\n' + policy[policy.index("// Translation-unit-local"): ] + '\n#endif\n'
    for name in ("ecc", "ecc_dh", "ecc_dsa", "esp_tinycrypt_port"):
        source += '#include "' + (P4 / "tinycrypt" / (name + ".inc.h")).as_posix() + '"\n'
    source += '''
static int kat_compute(const uint8_t*,uint8_t*,uECC_Curve);
static int kat_valid(const uint8_t*,uECC_Curve);
static int kat_shared(const uint8_t*,const uint8_t*,uint8_t*,uECC_Curve);
#define uECC_compute_public_key kat_compute
#define uECC_valid_public_key kat_valid
#define uECC_shared_secret kat_shared
'''
    self_test = (P4 / "OfeP4BleEccSelfTest.c").read_text()
    source += self_test[self_test.index("#include <tinycrypt/ecc.h>"):]
    source += '\n#undef uECC_compute_public_key\n#undef uECC_valid_public_key\n#undef uECC_shared_secret\n'
    source += '\n#include "' + (ROOT / "tools/tests/display_hybrid_native/test_p4_ecc.c").as_posix() + '"\n'
    cpp = OUT / "test.c"
    cpp.write_text(source, encoding="utf-8")
    vc = Path("C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207")
    kit = Path("C:/Program Files (x86)/Windows Kits/10")
    ver = "10.0.26100.0"
    env = dict(os.environ)
    env["INCLUDE"] = ";".join(str(p) for p in (vc / "include", kit / f"Include/{ver}/ucrt", kit / f"Include/{ver}/shared", kit / f"Include/{ver}/um"))
    env["LIB"] = ";".join(str(p) for p in (vc / "lib/x64", kit / f"Lib/{ver}/ucrt/x64", kit / f"Lib/{ver}/um/x64"))
    for name, flags in (("software", []), ("modeled-hw", ["/DOFE_TEST_HW=1"])):
        exe = OUT / (name + ".exe")
        subprocess.run([str(vc / "bin/Hostx64/x64/cl.exe"), "/nologo", "/TC", "/std:c17", "/utf-8", "/W4", "/O2", *flags,
                    "/I"+str(OUT), "/I"+str(SDK / "include/bt/common/tinycrypt/include"), str(cpp),
                        "/Fo"+str(OUT)+"/", "/Fe"+str(exe)], env=env, check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
