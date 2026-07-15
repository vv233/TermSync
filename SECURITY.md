# Security Policy

## Supported versions

TermSync is pre-release software. Security fixes target the latest release and
`main`. There is no long-term support branch yet.

| Version | Supported |
|---|---|
| `main` / latest pre-release | ✅ |
| Older tags | ❌ |

## Reporting a vulnerability

**Please do not open a public issue for security problems.**

Report privately through GitHub's
[**Report a vulnerability**](https://github.com/vv233/TermSync/security/advisories/new)
(Security → Advisories) so it can be triaged and fixed before disclosure.

Include as much as you can:

- Affected version and platform.
- A description of the issue and its impact (e.g. credential exposure, remote
  code execution, host-key bypass, path traversal in transfers).
- Steps or a proof of concept to reproduce.

We aim to acknowledge reports within a few days and to coordinate a fix and
disclosure timeline with you.

## Scope and notes

TermSync is a client that connects to remote systems, so a few areas are
especially security-sensitive — reports here are very welcome:

- **Credential handling:** passwords/passphrases are stored in the OS credential
  vault (Windows Credential Manager); only an opaque reference is kept in the
  profile database. Report anything that logs, leaks, or persists secrets in
  plaintext.
- **Host-key trust:** connections use trust-on-first-use and warn on key
  changes. Report ways to bypass verification or send credentials to an
  unverified host.
- **Transfers:** report path traversal, symlink, or overwrite issues in SFTP /
  FTP / SCP handling.
- **Scripting:** the automation engine runs user-provided scripts against a
  session; report sandbox-escape-style concerns.

Third-party cryptography and transport come from libssh2, libcurl, and OpenSSL;
vulnerabilities in those should also be reported upstream.
