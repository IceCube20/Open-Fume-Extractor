# Security Policy

## Supported releases

Security fixes target the latest published firmware versions. Beta and alpha
firmware should be treated as pre-release software.

## Reporting a vulnerability

Do not publish credentials, private keys, device backups or exploitable details
in a public issue. Contact the repository owner privately through the security
reporting channel configured on GitHub.

## Security boundaries

- The local web interface uses HTTP and is intended for a trusted LAN.
- MQTT can use TLS with a configured CA certificate.
- Firmware packages are authenticated with Ed25519 before update.
- The repository contains only the public verification key. The release-signing
  private key is not part of the source tree.
- Developer mode is a maintenance aid, not a strong security boundary. Its
  password is embedded in firmware and can be recovered from a binary. Do not
  expose the web interface to untrusted networks.
- Exported backups contain WiFi, web and MQTT credentials and CA certificates.
  Store them as secrets and never attach them to public issues.

