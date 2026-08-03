#!/bin/bash
#
# Grow the root partition/filesystem so that it spans the whole boot disk.
# CloudLab images ship a small root partition and leave the rest of the disk to
# an `emulab` LVM volume group mounted at /mydata; the experiments need the
# space on / instead.  Re-running this script is a no-op.

set -e
set -x

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=config.sh
source "${SCRIPT_DIR}/config.sh"

DISK=${LITELIB_ROOT_DISK}
PART=${LITELIB_ROOT_PART}
ROOT_DEV="${DISK}${PART}"

if [ -z "${DISK}" ] || [ -z "${PART}" ] || [ ! -b "${ROOT_DEV}" ]; then
  echo "cannot determine the root partition (got '${ROOT_DEV}')." 1>&2
  echo "Override it explicitly, e.g. LITELIB_ROOT_DISK=/dev/sdb LITELIB_ROOT_PART=3 $0" 1>&2
  exit 1
fi
echo "Root filesystem lives on ${ROOT_DEV} (disk ${DISK}, partition ${PART})."

install_dependencies() {
  if command -v growpart >/dev/null 2>&1; then
    echo "cloud-guest-utils already installed, skipping."
    return
  fi
  litelib_phase "installing cloud-guest-utils"
  apt-get install "${APT_OPTS[@]}" cloud-guest-utils
}

remove_lvm() {
  echo "Unmounting /mydata..."
  umount /mydata # Continue if already unmounted

  echo "Removing LVM volume group and physical volumes..."
  vgremove -f emulab
  pvremove "${DISK}4" /dev/sdb
}

delete_extra_partition() {
  echo -e "d\n4\nw" | fdisk "${DISK}"
}

extend_root_partition() {
  litelib_phase "growing ${ROOT_DEV} to fill ${DISK}"
  # growpart reports "already as large as it can be" as a *failure* exit code
  # with a NOCHANGE message, so the message -- not the exit status -- is what
  # distinguishes "nothing to do" from a real problem (e.g. the wrong disk was
  # picked and sfdisk cannot read its partition label).
  local rc=0
  local out
  out=$(growpart "${DISK}" "${PART}" 2>&1) || rc=$?
  echo "${out}"
  if [ "${rc}" -eq 0 ]; then
    return 0
  fi
  if echo "${out}" | grep -qi "NOCHANGE"; then
    echo "${ROOT_DEV} already spans ${DISK}, nothing to do."
    return 0
  fi
  echo "growpart ${DISK} ${PART} failed (exit ${rc})" 1>&2
  return "${rc}"
}

resize_filesystem() {
  litelib_phase "resizing the root filesystem"
  echo "Updating kernel's partition table..."
  partprobe "${DISK}"

  echo "Resizing the filesystem..."
  resize2fs "${ROOT_DEV}"
}

update_fstab() {
  if ! grep -q '/mydata' /etc/fstab; then
    echo "/etc/fstab already free of /mydata entries, skipping."
    return
  fi
  [ -f /etc/fstab.backup ] || cp /etc/fstab /etc/fstab.backup
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
  # The two steps below are only needed on CloudLab profiles that put the spare
  # disk space into the `emulab` LVM volume group mounted at /mydata.
  # remove_lvm
  # delete_extra_partition
  extend_root_partition
  resize_filesystem
  update_fstab
  display_status

  echo "Root filesystem resized successfully."
}

main
