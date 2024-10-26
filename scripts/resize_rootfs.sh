#!/bin/bash

set -e
set -x

install_dependencies() {
  apt install -y cloud-guest-utils
}

remove_lvm() {
  echo "Unmounting /mydata..."
  umount /mydata # Continue if already unmounted

  echo "Removing LVM volume group and physical volumes..."
  vgremove -f emulab
  pvremove /dev/sda4 /dev/sdb
}

delete_sda4() {
  echo -e "d\n4\nw" | fdisk /dev/sda
}

extend_sda3() {
  growpart /dev/sda 3
}

resize_filesystem() {
  echo "Updating kernel's partition table..."
  partprobe /dev/sda

  echo "Resizing the filesystem..."
  resize2fs /dev/sda3
}

update_fstab() {
  cp /etc/fstab /etc/fstab.backup
  sed -i '/\/mydata/d' /etc/fstab
  echo "Updated /etc/fstab. Original backed up as /etc/fstab.backup"
}

display_status() {
  echo "Process completed. Please reboot the system for changes to take full effect."
  echo "Current partition and filesystem status:"
  echo "----------------------------------------"
  lsblk
  echo "----------------------------------------"
  df -h
  echo "----------------------------------------"
}

main() {
  install_dependencies
  remove_lvm
  delete_sda4
  extend_sda3
  resize_filesystem
  update_fstab
  display_status
}

main
