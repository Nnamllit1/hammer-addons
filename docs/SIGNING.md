# Offline publisher signatures

Hammer Addons supports author-generated publisher keys and offline package
verification. No registration, certificate purchase, account or central server is
required. Authors can sign locally; users can verify without a network connection.

A signature binds the package contents to a publisher key. Its SHA-256 fingerprint
is the stable publisher identifier. The signed name, contact address and website
are **self-declared**: the framework does not verify legal identity, ownership of
an email address or profile, or who uploaded the package. Anyone can claim the
same display name with a different key. Compare fingerprints, not names.

## For users

The Add-ons tab shows **Signature**, **Publisher** and **Publisher key (SHA-256)**.
Hover over the publisher columns for the signed contact and website details.
These details are displayed as text; the manager does not fetch profiles or open
links automatically.

- **Unapproved (unsigned):** no publisher signature or local approval is present;
  no DLL code is loaded until you approve the package.
- **Approved locally (unsigned):** these exact package contents were approved for
  your Windows user. This is not an author signature or publisher certificate.
- **Valid (self-declared publisher):** the signature and exact package inventory
  verified, but the key has not been pinned locally. First-use publisher approval
  is required before loading.
- **Valid (locally pinned key):** the package also matches your saved key for this
  add-on ID. This is key continuity, not verification of a person's identity.
- **Invalid:** signature verification failed. The loader refuses to load the DLL.

Other failures, including a publisher-pin mismatch, appear in Details. A valid
signature is not a safety endorsement: a malicious author can sign malicious code.
Native DLLs are still unsandboxed.

To check a package without executing its DLL, use the bundled tool:

```powershell
.\addon_sign.exe verify ".\addons\example"
```

Obtain the publisher fingerprint through a channel you already trust, such as a
previously verified release or a direct exchange with the author. A fingerprint
included beside an unknown download does not independently authenticate it.
Compare it explicitly:

```powershell
.\addon_sign.exe verify ".\addons\example" --publisher "FULL_64_CHARACTER_FINGERPRINT"
```

Replace the placeholder with the full lowercase hexadecimal fingerprint. To
require this key on subsequent launches and updates, pin it locally:

```powershell
.\addon_sign.exe pin ".\addons\example" --publisher "FULL_64_CHARACTER_FINGERPRINT" --store ".\publisher-pins"
```

Run this from the portable launcher folder. The tool verifies the package before
creating `publisher-pins/example.sha256`. It refuses to overwrite an existing pin.
The loader then rejects missing signatures and different keys for that add-on ID,
even if someone reuses the same claimed author name. New IDs are not automatically
covered by a pin for an existing add-on.

Keep `publisher-pins/` when updating the portable launcher. Do not replace this
folder with files supplied by an add-on. Key rotation requires independently
confirming the new fingerprint before manually replacing the old local pin.
A valid but unfamiliar publisher key also prompts for approval in the launcher;
accepting pins that key for this add-on ID and its future signed updates. Anyone
can create a key, so self-signing does not bypass first-use approval. A different
key or missing signature for a pinned add-on is blocked without an automatic
replacement prompt.

For unsigned packages, `verify` exits with code 2; invalid signatures, mismatched
expected keys and other errors return 1. Successful verification returns 0.

## Approve unsigned add-ons

Before starting Workshop Tools, the launcher asks whether to approve each new
unsigned package. The dialog shows the add-on ID, folder and full package hash,
and explains that native code runs with your Windows permissions. **No** is the
default. Declining leaves that add-on unloaded; the other tools can still start.
This also applies to unsigned bundled examples on their first launch.

Accepting creates a DPAPI-protected receipt under `local-approvals/<id>.approval`
in the portable launcher folder. It belongs to your Windows user and records the
exact manifest, DLLs, dependencies and assets. Nothing is installed in the Windows
certificate store. Unchanged packages reuse that approval automatically.

If anything in the package changes, loading fails. The launcher does not repeatedly
ask you to accept changed or corrupted contents. After reviewing an intentional
update, close Workshop Tools and explicitly approve its new contents:

```powershell
.\addon_sign.exe approve ".\addons\example" --store ".\local-approvals"
```

This command is an explicit trust decision and replaces the local receipt for
that add-on. It does not override an invalid publisher signature or a pinned-key
requirement. The runtime verifies receipts again before loading; bypassing the
launcher does not bypass approval. Missing, unreadable or corrupted receipts do
not grant permission. A corrupted or foreign-user receipt requires explicit
reapproval.

Keep `local-approvals/` locally across launcher updates on the same account; do
not publish it or install someone else's receipts. Removing a receipt makes an
unsigned package require first-use approval again. A receipt is not a sandbox:
malicious code already running as your user can access that user's trust state.
The same package size/path restrictions listed below apply to local approvals.

## For authors

`addon_sign.exe` is included in both the portable and SDK ZIPs. Generate a key in a
private directory outside your package, for example:

```powershell
New-Item -ItemType Directory "$env:USERPROFILE\HammerAddonKeys"
.\addon_sign.exe keygen "$env:USERPROFILE\HammerAddonKeys\publisher.hakey"
```

Keep the printed fingerprint and reuse this key across your releases. The private
key is encrypted with Windows DPAPI for the current user, normally on this machine;
it is never put in the signature. Do not commit or distribute it. DPAPI does not
protect it from malicious software already running as that user. Copying the local
`.hakey` file alone is not a portable recovery method. Use a
password-encrypted `.hapkey` export for backups, other Windows machines or CI,
as described below. Keep the password separately. Key rotation certificates
are not implemented.

Prepare a clean final add-on folder with `addon.ini`, the DLL and all bundled
dependencies/assets. Then sign it:

```powershell
.\addon_sign.exe sign ".\package\example" `
  --key "$env:USERPROFILE\HammerAddonKeys\publisher.hakey" `
  --name "Example Developer" `
  --contact "developer@example.org" `
  --website "https://example.org/projects"
.\addon_sign.exe verify ".\package\example"
```

Only `--name` is required publisher information. Contact and website are optional;
include information you intend to make public. A pseudonym or team name is valid.
The generated `addon.signature` contains the public key, author claims, file
inventory and signature. Distribute it with the unchanged folder. No private key
or Windows certificate installation is needed by users.

Sign a fresh staging folder for each version. Signing refuses to overwrite an
existing signature or key. Any changed, removed or added package file invalidates
the signature, including editing `addon.ini` or swapping a dependency. Keep logs
and mutable settings outside the package. To disable a signed add-on, move its
folder outside `addons/`; editing its signed manifest invalidates the signature.

## Portable keys and CI

Export the existing key with a strong, separate password. Export/import preserves
its public key and publisher fingerprint. The password is read from a named
environment variable, never a command-line password argument:

```powershell
$env:HA_KEY_PASSWORD = [Net.NetworkCredential]::new('', (Read-Host "Portable key password" -AsSecureString)).Password
try {
  .\addon_sign.exe export-key "$env:USERPROFILE\HammerAddonKeys\publisher.hakey" `
    --output "$env:USERPROFILE\HammerAddonKeys\publisher.hapkey" `
    --password-env HA_KEY_PASSWORD
} finally {
  Remove-Item Env:HA_KEY_PASSWORD
}
```

Use a randomly generated password of at least 32 characters. The tool rejects
exports below 16 UTF-8 bytes and refuses to overwrite existing key files. The
`.hapkey` file is encrypted using AES-256-GCM, with a key derived by
PBKDF2-HMAC-SHA-256 (600,000 iterations), a random 16-byte salt and a random 12-byte
nonce. The 16-byte authentication tag detects wrong passwords and modified key
files. Version, salt and nonce are authenticated. Windows CNG implements these
primitives; the portable envelope is specific to this tool, not a PFX/PKCS#8 file.

To use that key on another Windows machine, either sign directly with
`--key publisher.hapkey --password-env HA_KEY_PASSWORD`, or import it into local
DPAPI storage:

```powershell
.\addon_sign.exe import-key ".\publisher.hapkey" --output ".\publisher.hakey" --password-env HA_KEY_PASSWORD
```

For a Windows CI runner, store the encrypted file as a base64 CI secret
`HA_PUBLISHER_KEY_B64`, with its password in a separate secret `HA_KEY_PASSWORD`.
After building `addon_sign.exe` and preparing the final add-on folder, a GitHub
Actions signing step can be:

```yaml
- name: Sign add-on package
  shell: pwsh
  env:
    HA_PUBLISHER_KEY_B64: ${{ secrets.HA_PUBLISHER_KEY_B64 }}
    HA_KEY_PASSWORD: ${{ secrets.HA_KEY_PASSWORD }}
  run: |
    $keyFile = Join-Path $env:RUNNER_TEMP 'publisher.hapkey'
    try {
      [IO.File]::WriteAllBytes($keyFile, [Convert]::FromBase64String($env:HA_PUBLISHER_KEY_B64))
      & .\build\native\Release\addon_sign.exe sign .\package\example --key $keyFile --password-env HA_KEY_PASSWORD --name "Example Developer"
      if ($LASTEXITCODE) { throw 'Package signing failed' }
    } finally {
      Remove-Item -LiteralPath $keyFile -ErrorAction SilentlyContinue
    }
```

Give signing secrets only to trusted release jobs, not untrusted pull-request
code. Neither key form belongs in the repository or published artifacts. Keep an
encrypted backup and its password independently of CI. Verification remains fully
offline and needs neither secret. The CLI currently runs on Windows x64 runners.

## Format and verification boundary

Version 1 uses ECDSA P-256 over SHA-256 through Windows CNG. The signed payload
starts with `HAMMER-ADDONS-SIGNATURE-1` and LF, followed in order by LF-terminated
`key=`, `name=`, `contact=`, `website=` and sorted `file=` lines. There are no BOMs,
CR characters or optional whitespace. The `signature=` trailer is excluded from
the payload hash and must be the final LF-terminated line.

The public key is a 65-byte uncompressed SEC1 point (`04 || X || Y`), encoded in
lowercase hex. Its SHA-256 is the publisher fingerprint. Publisher text is UTF-8
encoded as lowercase hex; each file line is a 64-character SHA-256, one ASCII
space, then the hex-encoded relative path. File entries sort by their ASCII path.
The ECDSA signature is 64 bytes, `r || s`, each a 32-byte big-endian integer,
encoded as lowercase hex. Unknown versions and noncanonical envelopes are rejected.

The inventory covers every regular package file except root `addon.signature`,
including dependencies and assets. Version 1 supports at most 1,024 files and
1 GiB total content; relative paths use portable ASCII components and are at most
240 characters. Names may contain letters, digits, spaces, underscores, hyphens
and dots, with no trailing spaces/dots. Reparse points, hard-linked files and
case-colliding file paths are rejected. Alternate data streams are rejected except
Windows' `Zone.Identifier`, which is not signed or removed. Empty directories and
filesystem attributes are not signed.

Verification happens before native loading. Checked files and package directories
are held open without write/delete sharing until the DLL has loaded. This reduces
ordinary verification/load races; it is not protection from a malicious process
with equivalent permissions, a compromised loader or another loaded native add-on.
The inventory does not attest to Windows/host modules, already-loaded dependencies,
or files that an add-on accesses outside its package later.

This is a Hammer Addons package signature, **not Windows Authenticode**. It does
not grant Windows publisher reputation, guarantee antivirus acceptance or change
VAC compatibility. Fully offline verification cannot discover newly revoked keys,
prove a signing time or establish that a release is the newest available version.
Revocation decisions and trusted fingerprint changes must be distributed and
applied separately.

Implementation references: [Windows CNG signing](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptsignhash)
and [DPAPI key protection](https://learn.microsoft.com/en-us/windows/win32/api/dpapi/nf-dpapi-cryptprotectdata).
