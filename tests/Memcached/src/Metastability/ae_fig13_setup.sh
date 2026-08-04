#!/bin/bash
#
# Figure 13 -- STEP 1 of 3: set up the four-container Metastability benchmark.
#
# The 34.6M-row MySQL database takes hours to create. It is initialized once
# into the named Docker volume `mysql_data` and preserved thereafter. All
# other state is reset/restarted before every measured arm by ae_fig13_run.sh.

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=ae_fig13_common.sh
source "${SCRIPT_DIR}/ae_fig13_common.sh"

DB_ENTRIES=${FIG13_DB_ENTRIES:-1400000}
DB_ARCHIVE_DIR=${FIG13_DB_ARCHIVE_DIR:-${FIG13_RESULTS_DIR}/database}
DB_ARCHIVE=${FIG13_DB_ARCHIVE:-${DB_ARCHIVE_DIR}/mysql-${DB_ENTRIES}-rows.tar.zst}
STAGES=("$@")
if [ "${#STAGES[@]}" -eq 0 ]; then
  STAGES=(containers ssh mysql memcached web client)
fi

setup_ssh() {
  fig13_info "configuring key-only SSH from client to mysql/memcached"
  fig13_docker exec client bash -lc \
    'apt-get update -qq &&
     DEBIAN_FRONTEND=noninteractive apt-get install -y -qq openssh-client'
  for c in mysql memcached; do
    fig13_docker exec "$c" bash -lc \
      'apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq openssh-server &&
       mkdir -p /run/sshd /root/.ssh && chmod 700 /root/.ssh &&
       service ssh start'
  done
  fig13_docker exec client bash -lc \
    'mkdir -p /root/.ssh && chmod 700 /root/.ssh;
     test -s /root/.ssh/id_ed25519 ||
       ssh-keygen -q -t ed25519 -N "" -f /root/.ssh/id_ed25519'
  pub=$(fig13_docker exec client cat /root/.ssh/id_ed25519.pub)
  for c in mysql memcached; do
    fig13_docker exec "$c" bash -lc \
      "grep -qF '${pub}' /root/.ssh/authorized_keys 2>/dev/null ||
       echo '${pub}' >> /root/.ssh/authorized_keys;
       chmod 600 /root/.ssh/authorized_keys"
  done
  fig13_docker exec client bash -lc \
    'touch /root/.ssh/known_hosts;
     for h in mysql memcached; do
       ssh-keygen -F "$h" >/dev/null 2>&1 ||
         ssh-keyscan -T 10 "$h" >> /root/.ssh/known_hosts 2>/dev/null;
     done'
}

mysql_volume_source() {
  fig13_docker inspect mysql --format \
    '{{range .Mounts}}{{if eq .Destination "/var/lib/mysql"}}{{.Source}}{{end}}{{end}}'
}

verify_mysql_archive() {
  [ -s "${DB_ARCHIVE}" ] && [ -s "${DB_ARCHIVE}.sha256" ] || return 1
  (
    cd "$(dirname "${DB_ARCHIVE}")"
    sha256sum -c "$(basename "${DB_ARCHIVE}").sha256" >/dev/null
  )
}

archive_mysql() {
  if verify_mysql_archive; then
    fig13_ok "verified database archive: ${DB_ARCHIVE}"
    return 0
  fi

  local source tmp
  source=$(mysql_volume_source)
  case "${source}" in
  /var/lib/docker/volumes/*/_data) ;;
  *) fig13_die "refusing to archive unexpected MySQL volume path: ${source}" ;;
  esac

  mkdir -p "${DB_ARCHIVE_DIR}"
  tmp="${DB_ARCHIVE}.tmp"
  rm -f "${tmp}"
  fig13_info "stopping MySQL container for a consistent one-time database archive"
  fig13_docker stop mysql >/dev/null
  if ! sudo -n tar -I 'zstd -T0 -3' -C "${source}" -cf "${tmp}" .; then
    fig13_docker start mysql >/dev/null || true
    rm -f "${tmp}"
    return 1
  fi
  sudo -n chown "$(id -u):$(id -g)" "${tmp}"
  mv "${tmp}" "${DB_ARCHIVE}"
  (
    cd "${DB_ARCHIVE_DIR}"
    sha256sum "$(basename "${DB_ARCHIVE}")" \
      > "$(basename "${DB_ARCHIVE}").sha256"
  )
  fig13_docker start mysql >/dev/null
  verify_mysql_archive ||
    fig13_die "database archive checksum verification failed"
  fig13_ok "database archived before any experiment: ${DB_ARCHIVE}"
}

restore_mysql_archive() {
  verify_mysql_archive || return 1
  local source
  source=$(mysql_volume_source)
  case "${source}" in
  /var/lib/docker/volumes/*/_data) ;;
  *) fig13_die "refusing to restore unexpected MySQL volume path: ${source}" ;;
  esac

  fig13_info "restoring initialized database from ${DB_ARCHIVE}"
  fig13_docker stop mysql >/dev/null || true
  # This is a dedicated, explicitly resolved Docker volume mount -- never a
  # repository or broad filesystem directory.
  sudo -n find "${source}" -mindepth 1 -maxdepth 1 -exec rm -rf -- {} +
  sudo -n tar -I 'zstd -T0' -C "${source}" -xf "${DB_ARCHIVE}"
  fig13_docker start mysql >/dev/null
  fig13_docker exec mysql test -f /var/lib/mysql/.litelib_ae_initialized ||
    fig13_die "restored database is missing its initialization marker"
  fig13_ok "database restored from archive"
}

setup_mysql() {
  if fig13_docker exec mysql test -f /var/lib/mysql/.litelib_ae_initialized; then
    fig13_ok "MySQL data volume already initialized; preserving database files"
    archive_mysql
    fig13_docker update --cpus 4 mysql >/dev/null
    fig13_docker exec mysql service mysql start || true
    return
  fi
  if verify_mysql_archive; then
    restore_mysql_archive
    fig13_docker update --cpus 4 mysql >/dev/null
    return
  fi
  fig13_info "initializing ${DB_ENTRIES}-row MySQL database (first run: several hours)"
  # The 4-CPU limit is part of the measured experiment, not dataset creation.
  # Use every host CPU for the one-time load and restore the limit afterwards.
  fig13_docker update --cpus "$(nproc)" mysql >/dev/null
  if ! fig13_docker exec mysql bash -lc \
      "cd /workspace/setup_scripts &&
       ./setup_mysql.sh '%' '${DB_ENTRIES}'"; then
    fig13_docker update --cpus 4 mysql >/dev/null
    return 1
  fi
  fig13_docker exec mysql touch /var/lib/mysql/.litelib_ae_initialized
  # Archival is deliberately the first action after initialization. No other
  # setup stage or experiment may run before this completes and verifies.
  archive_mysql
  fig13_docker update --cpus 4 mysql >/dev/null
  fig13_ok "MySQL database initialized and marked in persistent volume"
}

setup_memcached() {
  # Generated helpers live on the bind mount and must be refreshed even when
  # CRIU/LiteMemcached binaries are already built.
  fig13_docker exec memcached bash -lc \
    'cd /workspace/setup_scripts;
     cp ../Memcached_codes/warm_up_cache.py.template ../Memcached_codes/warm_up_cache.py;
     sed -i "/warm_up_size =/c\warm_up_size = 140000" ../Memcached_codes/warm_up_cache.py;
     sed -i "s/node1:11211/127.0.0.1:11211/" ../Memcached_codes/warm_up_cache.py;
     ln -sfn /workspace/Memcached_codes/warm_up_cache.py /root/warm_up_cache.py;
     ln -sfn /workspace/Memcached_codes/crash.py /root/crash.py;
     ln -sfn /workspace/Memcached_codes/monitor.py /root/monitor.py;
     ln -sfn /workspace/Memcached_codes/init.py /root/init.py'
  if fig13_docker exec memcached test -x /root/LiteMemcached 2>/dev/null; then
    fig13_ok "Memcached/LiteMemcached environment already built"
    return
  fi
  fig13_info "installing Memcached, CRIU and building LiteMemcached"
  fig13_docker exec memcached bash -lc \
    'cd /workspace/setup_scripts && ./setup_memcached.sh'
}

setup_web() {
  # Always refresh generated PHP/config/symlinks from the current branch. The
  # base image already contains php-fpm, so checking only for the binary would
  # incorrectly skip setup and serve the image's default landing page.
  fig13_info "refreshing nginx/PHP web tier"
  fig13_docker exec web bash -lc \
    'cd /workspace/setup_scripts && ./setup_server.sh mysql memcached 10000'
}

setup_client() {
  # Always regenerate run_experiment.py and the trace sources from their
  # templates. The binary build is incremental, while skipping this stage
  # would leave an older generated script after an AE branch update.
  fig13_info "refreshing and building load generator"
  fig13_docker exec client bash -lc \
    "cd /workspace/setup_scripts &&
     ./setup_client.sh web memcached mysql '${DB_ENTRIES}'"
}

main() {
  local stage
  for stage in "${STAGES[@]}"; do
    case "${stage}" in
    containers)
      fig13_info "starting Figure 13 containers"
      fig13_compose up -d
      ;;
    ssh) setup_ssh ;;
    mysql) setup_mysql ;;
    memcached) setup_memcached ;;
    web) setup_web ;;
    client) setup_client ;;
    *) fig13_die "unknown setup stage: ${stage}" ;;
    esac
  done
  fig13_ok "Figure 13 environment ready; MySQL volume will be preserved"
}

main 2>&1 | tee "${FIG13_LOGS_DIR}/fig13-setup-$(fig13_run_id).log"
exit "${PIPESTATUS[0]}"
