#include <assert.h>
#include <stdio.h>
#include <tinycrypt/ecc_dsa.h>
#include "ecc_vectors.h"

static uint8_t hw_x[32], hw_y[32], hw_k[32];
static int hw_locked, hw_calls, hw_mode, corrupt_hw;
static int kat_fault;
static int kat_compute(const uint8_t* private_key,uint8_t* point,uECC_Curve curve) {
    const int base = memcmp(private_key,test_private[0],32) ? -1020 : -1010;
    if (kat_fault==base-1) return 0;
    int result=uECC_compute_public_key(private_key,point,curve);
    if (kat_fault==base-2) point[0]^=1;
    return result;
}
static int kat_valid(const uint8_t* point,uECC_Curve curve) {
    const int base = memcmp(point,test_public[1],64) ? -1020 : -1010;
    return kat_fault==base-3 ? -1 : uECC_valid_public_key(point,curve);
}
static int kat_shared(const uint8_t* point,const uint8_t* key,uint8_t* secret,uECC_Curve curve) {
    const int base = memcmp(key,test_private[0],32) ? -1020 : -1010;
    if (kat_fault==base-4) return 0;
    int result=uECC_shared_secret(point,key,secret,curve);
    if (kat_fault==base-5) secret[0]^=1;
    return result;
}
static void generator(uint8_t* point) {
    uECC_Curve curve=uECC_secp256r1();
    uECC_vli_nativeToBytes(point,32,curve->G);
    uECC_vli_nativeToBytes(point+32,32,curve->G+NUM_ECC_WORDS);
}
static int same_le(const uint8_t* little, const uint8_t* big) {
    for (unsigned i=0; i<32; ++i) if (little[i]!=big[31-i]) return 0;
    return 1;
}
void esp_crypto_ecc_lock_acquire(void) { assert(!hw_locked); hw_locked=1; }
void esp_crypto_ecc_lock_release(void) { assert(hw_locked); hw_locked=0; }
void ecc_ll_enable_bus_clock(bool on) { (void)on; }
void ecc_ll_power_up(void) {}
void ecc_ll_power_down(void) {}
void ecc_ll_reset_register(void) {}
void ecc_hal_set_mode(ecc_mode_t mode) { hw_mode=mode; }
void ecc_hal_start_calc(void) { assert(hw_locked); }
bool ecc_hal_is_calc_finished(void) { return true; }
void ecc_hal_enable_constant_time_point_mul(bool enabled) { assert(enabled); }
void ecc_hal_write_verify_param(const uint8_t* x,const uint8_t* y,uint8_t n) {
    assert(n==32); memcpy(hw_x,x,n); memcpy(hw_y,y,n);
}
int ecc_hal_read_verify_result(void) {
    assert(hw_mode==ECC_MODE_VERIFY);
    // Model the hardware boundary with the independently generated public points.
    for (unsigned i=0; i<32; ++i) {
        if (same_le(hw_x,vectors[i].public_a) && same_le(hw_y,vectors[i].public_a+32)) return 1;
        if (same_le(hw_x,vectors[i].public_b) && same_le(hw_y,vectors[i].public_b+32)) return 1;
    }
    uint8_t point[64]; generator(point);
    return same_le(hw_x,point) && same_le(hw_y,point+32);
}
void ecc_hal_write_mul_param(const uint8_t* k,const uint8_t* x,const uint8_t* y,uint8_t n) {
    assert(n==32); memcpy(hw_k,k,n); memcpy(hw_x,x,n); memcpy(hw_y,y,n); ++hw_calls;
}
int ecc_hal_read_mul_result(uint8_t* x,uint8_t* y,uint8_t n) {
    assert(n==32 && hw_mode==ECC_MODE_POINT_MUL);
    uint8_t point[64]; generator(point);
    assert(same_le(hw_x,point) && same_le(hw_y,point+32));
    // The HW public-key path receives a canonical scalar, never k+n / 257 bits.
    for (unsigned i=0; i<32; ++i) if (same_le(hw_k,vectors[i].private_a)) {
        for (unsigned j=0; j<32; ++j) {
            x[j]=vectors[i].public_a[31-j]; y[j]=vectors[i].public_a[63-j];
        }
        if (corrupt_hw) x[0]^=1;
        return 0;
    }
    assert(!"Unexpected hardware scalar or byte order"); return -1;
}
static int blinding_rng(uint8_t* out,unsigned n) {
    // Deterministic, nonzero test-only Z. Never used by production firmware.
    memset(out,0,n); out[0]=7; return 1;
}
static int failed_rng(uint8_t* out,unsigned n) { (void)out; (void)n; return 0; }
static unsigned hw_rng_calls;
void esp_fill_random(void* out,size_t n) {
    // Native-only entropy fixture. Production uses the real P4 API.
    assert(out && n);
    ++hw_rng_calls;
    assert(blinding_rng(out,(unsigned)n));
}
int main(void) {
    // Verify the production fixtures independently, not against their own copy.
    assert(!memcmp(test_private[0],vectors[8].private_a,32));
    assert(!memcmp(test_private[1],vectors[9].private_a,32));
    assert(!memcmp(test_public[0],vectors[8].public_a,64));
    assert(!memcmp(test_public[1],vectors[9].public_a,64));
    // Reproduce the installed SDK's unavailable pre-host HCI RNG, then verify
    // the production self-test replaces it and leaves the P4 RNG installed.
    uECC_Curve curve=uECC_secp256r1();
    uint8_t before_host_secret[32];
    uint8_t before_host_public[64];
    uECC_set_rng(failed_rng);
    assert(uECC_compute_public_key(test_private[0],before_host_public,curve));
    assert(!memcmp(before_host_public,test_public[0],sizeof(before_host_public)));
    assert(!uECC_valid_public_key(test_public[1],curve));
    assert(!uECC_shared_secret(test_public[1],test_private[0],before_host_secret,curve));
    assert(ofe_p4_ble_ecc_self_test()==0);
    assert(uECC_get_rng()==ofe_p4_tinycrypt_rng && hw_rng_calls>0);
    assert(!ofe_p4_tinycrypt_rng(NULL,32));
    for (int i=0;i<2;++i) for (int step=1;step<=5;++step) {
        kat_fault=-1010-i*10-step;
        assert(ofe_p4_ble_ecc_self_test()==kat_fault);
    }
    kat_fault=0;
#if SOC_ECC_SUPPORTED
    corrupt_hw=1;
    assert(ofe_p4_ble_ecc_self_test()==-1012);
    corrupt_hw=0;
#endif
    assert(ofe_p4_ble_ecc_self_test()==0);
    for (unsigned blinded=0; blinded<2; ++blinded) {
        uECC_set_rng(blinded ? blinding_rng : NULL);
        unsigned rng_calls_before=hw_rng_calls;
        assert(ofe_p4_ble_ecc_self_test()==0);
        assert(uECC_get_rng()==ofe_p4_tinycrypt_rng && hw_rng_calls>rng_calls_before);
        uECC_set_rng(blinded ? blinding_rng : NULL);
        for (unsigned i=0; i<32; ++i) {
            uint8_t point[64],secret[32];
            assert(uECC_compute_public_key(vectors[i].private_a,point,curve));
            assert(!memcmp(point,vectors[i].public_a,64));
            assert(uECC_valid_public_key(point,curve)==0);
            int calls=hw_calls;
            assert(uECC_shared_secret(vectors[i].public_b,vectors[i].private_a,secret,curve));
            assert(!memcmp(secret,vectors[i].shared,32));
            assert(hw_calls==calls); // ECDH must use the regularized software ladder.
            assert(!hw_locked);
        }
    }
    uint8_t invalid[64]={0};
    assert(uECC_valid_public_key(invalid,curve)!=0);
    uint8_t private_key[32]={0},public_key[64];
    assert(!uECC_compute_public_key(private_key,public_key,curve));
    uECC_vli_nativeToBytes(private_key,32,curve->n);
    assert(!uECC_compute_public_key(private_key,public_key,curve));
    memset(invalid,0xff,sizeof(invalid));
    assert(uECC_valid_public_key(invalid,curve)!=0);
#if !SOC_ECC_SUPPORTED
    assert(uECC_make_key(public_key,private_key,curve));
    assert(!uECC_valid_public_key(public_key,curve));
    assert(uECC_compute_public_key(private_key,invalid,curve));
    assert(!memcmp(public_key,invalid,sizeof(public_key)));
#endif
    uint8_t hash[32]={0},signature[64];
    assert(uECC_sign(vectors[0].private_a,hash,sizeof(hash),signature,curve));
    assert(uECC_verify(vectors[0].public_a,hash,sizeof(hash),signature,curve));
    hash[0]=1;
    assert(!uECC_verify(vectors[0].public_a,hash,sizeof(hash),signature,curve));
    uECC_set_rng(failed_rng);
    assert(!uECC_shared_secret(vectors[0].public_b,vectors[0].private_a,hash,curve));
#if SOC_ECC_SUPPORTED
    puts("PASS: upstream HW regression, real port conversions, 64 OpenSSL ECDH vectors and scalar guard (HW modeled)");
#else
    assert(hw_calls==0 && !hw_locked);
    puts("PASS: production SOFTWARE ECC, pre-host RNG failure reproduced and P4 RNG handoff verified, 64 OpenSSL ECDH vectors, self-test failures, invalid peers, signing and blinding; ECC accelerator NOT used (entropy API modeled)");
#endif
    return 0;
}
