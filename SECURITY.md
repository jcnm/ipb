# Security Policy

## Supported Versions

The following versions of IPB are currently supported with security updates:

| Version | Supported          |
| ------- | ------------------ |
| main    | :white_check_mark: |
| develop | :white_check_mark: |

## Reporting a Vulnerability

We take the security of IPB seriously. If you discover a security vulnerability, please follow these steps:

### How to Report

1. **Do not** open a public GitHub issue for security vulnerabilities
2. Email security concerns to the maintainers via GitHub's private vulnerability reporting feature
3. Alternatively, open a [private security advisory](https://github.com/jcnm/ipb/security/advisories/new)

### What to Include

Please include the following information in your report:

- Type of vulnerability (e.g., buffer overflow, injection, authentication bypass)
- Location of the affected source code (file path and line number if possible)
- Step-by-step instructions to reproduce the issue
- Proof-of-concept or exploit code (if available)
- Impact assessment and potential attack scenarios

### Response Timeline

- **Acknowledgment**: Within 48 hours of receiving your report
- **Initial Assessment**: Within 7 days
- **Resolution Timeline**: Depends on severity
  - Critical: 7-14 days
  - High: 14-30 days
  - Medium: 30-60 days
  - Low: 60-90 days

### Disclosure Policy

- We follow coordinated disclosure practices
- We will work with you to understand and resolve the issue
- Public disclosure will occur after a fix is available
- Credit will be given to reporters who wish to be acknowledged

## Security Best Practices for Users

When deploying IPB in production:

1. **Keep Updated**: Always use the latest stable release
2. **Network Security**: Deploy behind appropriate firewalls
3. **TLS/SSL**: Enable encryption for MQTT communications
4. **Authentication**: Configure proper authentication for all endpoints
5. **Least Privilege**: Run services with minimal required permissions
6. **Monitoring**: Enable logging and monitoring for security events

## Security Scanning

This project uses automated security scanning:

- **CodeQL**: Static analysis for C++ vulnerabilities
- **OSSF Scorecard**: Supply chain security assessment
- **Flawfinder**: C/C++ security flaw detection
- **Gitleaks**: Secret detection
- **SBOM**: Software Bill of Materials generation

Security scan results are available in the GitHub Security tab.
