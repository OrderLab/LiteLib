findLastUsableCore() {
    count=`grep -c ^processor /proc/cpuinfo`
    count=$((count - 1))
    while [ "${count}" -ge "0" ] ; do
        taskset -c ${count} echo >/dev/null 2>&1
        if [ "$?" -eq "0" ];then
            echo ${count}
        fi
        count=$((count - 1))
    done
    return 0
}

findLastUsableCore