#!/usr/bin/perl -i

my $file = $ARGV[0];
my $text = do { local( @ARGV, $/ ) = $file ; <> } ;

$text =~ s/([^\n])\.sp/$1\n.sp/g;
$text =~ s/\.sp\n\.sp/.sp/g;
$text =~ s/\.sp\n\.SH/.SH/g;
$text =~ s/\.fi\n\.RE(?!\n\.sp)/.fi\n.sp\n.RE/g;

print $text;

