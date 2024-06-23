echo "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAINcZFT3064K5rGwyguWUb7oTMoHiogZRiXxR5h6le4jR yichencs@dell-yichencs" >> /root/.ssh/authorized_keys
echo "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIERwwdWQ79JOlU27J4wuC4xKk+vADhdOdwNaM9klmPcW toga@TOGA-ThinkpadT480s" >> /root/.ssh/authorized_keys
git config --global --add safe.directory /workspace
service ssh start
