#!/usr/bin/perl

use strict;
use warnings;
use POSIX qw/strftime/;

## Many thanks to Simon Lange for posting the original version of
## this script to the dbmail mailing list and giving permission to
## make it part of DBMail . I've modified it to be a little more
## general-purpose than Simon's original script, but all the good
## ideas here are his.
##  - Aaron Stone, December 2007

## Please set the correct path of dbmail-utilities
my $dbmailusers    =       "/usr/local/sbin/dbmail-users";

## We'll use the -M option to force delivery without
## consideration of quotas or sieve scripts.
my $delivery       =       "/usr/local/sbin/dbmail-smtp";

## Quota percentage when warning mails are sent
my $warnpercent    =       90;

## Mail Content
my $from           =       "postmaster";
my $date           =       strftime("%a, %d %b %Y %H:%M:%S %z", localtime);
my $msg            =       sub { return <<MSG };
Subject: Mail storage quota warning
Date: $date
From: $from
To: $_[0]

Dear $_[0],

You are currently using $_[1]% of your mail storage quota.

Please empty your trash and spam folders, delete old messages, or
otherwise remove messages from the server until you are under $_[2]%
usage of your mail storage quota.

  --  Postmaster
MSG

## !!!do not change anything below!!! ##
my @userinfo = `$dbmailusers -l`;
my $total = 0;
my $count = 0;

foreach (@userinfo) {
    chomp;

    last if m/^-- forwards --$/;

    # username : the letter 'x' : user id number : client id number : quota : used : comma, separated, aliases
    my ($username, $quota, $used) = m/^([^:]+):x:[^:]+:[^:]+:([^:]+):([^:]+)/ or next;

    $total++;

    next unless $quota > 0.00;

    my $percent =  $used / $quota * 100;

    next if $percent < $warnpercent;

    open (SMTP, "|$delivery -M Inbox -u $username") or die "Could not open $delivery: $!\n";
    print SMTP $msg->($username, sprintf("%.1f", $percent), $warnpercent);
    close(SMTP);

    $count++;
}

print "Sent $count warnings out of $total users.\n";
