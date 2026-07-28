# Release signing identity

The following identity signs official direct-distribution DingooPie Android
release APKs. The keystore and passwords are local secrets and must never be
committed. Back up the keystore separately before publishing an APK.

## Application identity

- Application ID: `com.dingoopie.android`
- Initial version code: `1`
- Initial version name: `1.0`
- Keystore type: PKCS12
- Key alias: `dingoopie-release`

## Certificate identity

- Subject and issuer: `CN=DingooPie Android, OU=Android, O=BL2CK, C=CN`
- Serial number: `4e869f45959cdc9b`
- Signature algorithm: SHA256withRSA
- Public key: RSA 3072 bits
- Valid from: July 28, 2026 08:31:21 HKT
- Valid until: July 4, 2126 08:31:21 HKT
- SHA-256: `2D:CC:1D:F8:D5:2E:6F:C8:8E:11:76:1A:24:D6:55:47:59:16:FA:A2:7E:36:60:21:3F:16:31:CE:99:64:11:6D`
- SHA-1: `64:EE:8E:CF:93:28:76:35:E2:50:BC:A7:F7:9C:E1:B9:A3:17:E6:A5`

Every future APK update must use this certificate. A build with a different
certificate cannot update an installed direct-distribution release with the
same application ID.

## Local files

The default untracked signing files are:

```text
.tools/signing/dingoopie-release.p12
.tools/signing/release-signing.properties
```

The properties file contains the keystore path, alias, and passwords. Keep both
files out of version control and store an encrypted offline backup.

## Verification

After building a release, verify the APK and compare the certificate SHA-256
fingerprint with the value above:

```powershell
.tools\android\sdk\build-tools\35.0.0\apksigner.bat verify `
    --verbose --print-certs `
    release\DingooPie-Android-v1.0-release.apk
```

The initial signed `1.0` APK uses v1 and v2 signing and has this file digest:

```text
SHA-256 A5C9DC5EB0852F2A75146DC2A8616E2D9B929EFB39960AB22A13F4DC8DC49393
```
