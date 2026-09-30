# Security and private data

## Report privately

Use GitHub's **Report a vulnerability** option in the repository Security tab when available. If private reporting is unavailable, open an issue asking for a private reporting channel without including the vulnerability details or any credentials. Never put tokens, calendars, tasks, SSIDs or device dumps in a public issue.

## Device credentials

This MVP uses credentials compiled into locally built firmware. Anyone who can read that firmware may recover those credentials. Do not upload your build output or share your configured device as though it contained no account access. Use your own account, a dedicated OAuth client and only the documented required scopes. Revoke tokens if a device or binary is lost or disclosed. No cloud credential service is provided by this project.

## Display privacy

The device is an account owner's display. Private calendar events that the authenticated account can read may appear with their details. The calendar's sharing controls are not a screen lock for the physical device. Use synthetic accounts/content for demonstrations and screenshots. The first release does not provide encrypted local application caches or a privacy lock.

## Public release checks

The publication process checks the selected source tree and distributable artifacts for credentials and personal content. It does not publish development history, personal firmware images or device backups. Maintainers should repeat these checks for each release; an ignored file is not automatically safe to package.
