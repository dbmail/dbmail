#!/bin/bash

LOGDIR_PREFIX="/tmp/valgrind_memcheck"

cleanup() {
    echo "Terminated, cleaning up"
    kill $imapd_pid &> /dev/null
    kill $memlog_pid &> /dev/null
    exit 1
}
trap cleanup INT

init_db() {

    COUNT=$1

    if [ -z $COUNT ] ; then
        COUNT=2000
    fi
    
    # create test users
    for user in tst_source tst_target; do 
        ./dbmail-users -d $user &> /dev/null
        ./dbmail-users -a $user -g 1 -w 123 &> /dev/null
    done

    # populate accounts with identical null messages (no need for logging)
    i=0
    echo "Populating database with $COUNT messages, please wait"
    while [ $i -lt $COUNT ] ; do
        num=$RANDOM
        echo | formail -a Message-ID: -a "Subject: $num" | ./dbmail-smtp -u tst_source tst_target &>/dev/null
        i=$[$i+1]
	echo -n "."
    done
}

wait_imapd_start() {
    echo -n "Waiting for dbmail-imapd: "
    unset imapd_pid
    for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
        echo -n "."
        sleep 1
        if [ -r /var/run/dbmail/dbmail-imapd.pid ] ; then 
            imapd_pid=$(cat /var/run/dbmail/dbmail-imapd.pid)
            break
        fi
    done
    
    if [ -z $imapd_pid ] ; then
        echo "DBMail did not start!"
        exit 1
    fi
    
    echo " done."
    
    echo -n "Spawning children... "
    sleep 5
    echo "done."
}

wait_imapd_stop() {
    echo -n "Waiting for dbmail-imapd to terminate: "
    while kill -0 $imapd_pid &> /dev/null ; do
        echo -n "."
        sleep 1
    done
    echo " done."
}

t_memcheck() {
    valgrind --tool=memcheck --leak-check=full --show-reachable=yes \
            --trace-children=yes --suppressions=contrib/dbmail.supp \
            --log-file="$LOGDIR/memcheck" \
            ./dbmail-imapd 
##&> /dev/null
}

t_massif() {
    valgrind --tool=massif --depth=7 \
            --alloc-fn=g_malloc --alloc-fn=g_realloc --alloc-fn=g_try_malloc \
            --alloc-fn=g_malloc0 --alloc-fn=g_mem_chunk_alloc \
            ./dbmail-imapd &> /dev/null
}

t_normal() {
    dbmail-imapd &> /dev/null
}

torture() {
    echo "Testing with $size messages"
    echo "G_SLICE=always_malloc: $malloc"
    echo "parent pid $imapd_pid"
    echo
    nice imapsync --host1 localhost --user1 tst_source --password1 123 \
         --host2 localhost --user2 tst_target --password2 123 \
         --delete2 --expunge2 \
         --syncinternaldates --subscribe \
         --skipheader 'X-DBMail-PhysMessage-ID' --skipsize \
         &> /dev/null
}

start_memlog() {
    log_fn="$LOGDIR/memlog.$imapd_pid"
    watch -n 10 "date >> $log_fn; ps -u dbmail u >> $log_fn; echo >> $log_fn" &> /dev/null &
    memlog_pid=$!
}

### RUN TESTS

mkdir -p "$LOGDIR_PREFIX"
chmod 777 "$LOGDIR_PREFIX"

for size in 5000; do 
    init_db $size
    
    for malloc in no yes ; do

        LOGDIR="$LOGDIR_PREFIX/${size}msgs"
    
        if [ "$malloc" = "yes" ] ; then
            G_SLICE="always-malloc"
            export G_SLICE
            LOGDIR="${LOGDIR}_always_malloc"
        else 
            unset G_SLICE
            export -n G_SLICE
        fi
        
        mkdir -p "$LOGDIR"
        chmod 777 "$LOGDIR"
        rm -f /var/log/dbmail/*

        t_memcheck
        wait_imapd_start
        
        start_memlog
    
        torture
        sleep 1
    
        kill $imapd_pid
        wait_imapd_stop
    
        mv /var/log/dbmail/dbmail.err "$LOGDIR/dbmail.err.$imapd_pid"
        
        kill $memlog_pid
        
        sync
    done
done
