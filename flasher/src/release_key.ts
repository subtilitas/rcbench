// The rcbench release public key, a copy of
// firmware/panel/main/keys/release_signing_pub.pem, the key the panel
// embeds. test/signature.test.ts holds the copy to that file byte for byte.
// Fingerprint, the SHA-256 of its DER form:
// cf9320e466c7961fca210b1d56f49b3f89c1ddc14a92f15bd62778f880f7eda2.

export const RELEASE_KEY_PEM = `-----BEGIN PUBLIC KEY-----
MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEdcxlH1PvUP3erFRha24yzwsduPkZ
vbD3TRrkPg4nImYS7wUel0X6sbKpM4DKpmSJCa9qqtxSXwyPI0HQvAf+cg==
-----END PUBLIC KEY-----
`;
