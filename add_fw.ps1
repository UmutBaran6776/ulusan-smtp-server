New-NetFirewallRule -DisplayName "Ulusan Mail Server SMTP" -Direction Inbound -LocalPort 25 -Protocol TCP -Action Allow
New-NetFirewallRule -DisplayName "Ulusan Mail Server IMAP" -Direction Inbound -LocalPort 143 -Protocol TCP -Action Allow
