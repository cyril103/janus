#!/usr/bin/env perl
use strict;
use warnings;
use Config;
use POSIX ();

sub usage {
    die "usage: observe-process.pl STATUS_FILE COMMAND [ARG ...]\n";
}

@ARGV >= 2 or usage();
my $status_file = shift @ARGV;
my @command = @ARGV;

my $pid = fork();
defined $pid or die "observe-process: fork failed: $!\n";
if ($pid == 0) {
    {
        no warnings 'exec';
        exec @command;
    }
    print STDERR "observe-process: exec $command[0] failed: $!\n";
    POSIX::_exit(127);
}

while (waitpid($pid, 0) == -1) {
    next if $!{EINTR};
    die "observe-process: waitpid failed: $!\n";
}
my $raw = $?;
my ($termination, $exit_code, $signal_number, $signal_name);
if ($raw & 127) {
    $termination = "signal";
    $signal_number = $raw & 127;
    my @names = split /\s+/, ($Config{sig_name} // "");
    $signal_name = $names[$signal_number] || "SIGNAL$signal_number";
    $signal_name = "SIG$signal_name" unless $signal_name =~ /^SIG/;
    $exit_code = 128 + $signal_number;
} else {
    $termination = "exit";
    $exit_code = $raw >> 8;
    $signal_number = 0;
    $signal_name = "";
}

my $temporary = "$status_file.$$";
open my $handle, ">", $temporary
    or die "observe-process: cannot write $temporary: $!\n";
printf {$handle} "termination=%s\nexit_code=%d\nsignal_number=%d\nsignal_name=%s\n",
    $termination, $exit_code, $signal_number, $signal_name;
close $handle or die "observe-process: cannot close $temporary: $!\n";
rename $temporary, $status_file
    or die "observe-process: cannot publish $status_file: $!\n";
exit $exit_code;
