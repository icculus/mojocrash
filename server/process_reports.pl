#!/usr/bin/perl -w -T

use warnings;
use strict;
use DBI;

my $dblink = undef;
my $dbhost = 'localhost';
my $dbuser = 'root';
my $dbpass = undef;
my $dbpassfile = 'dbpass.txt';

my %apps;
my %appvers;
my %plats;
my %platvers;
my %cpus;

# !!! FIXME: grep for 'die' and remove it. This needs to stay up in face of
# !!! FIXME:  temporary failures, and not take down other threads, too!

sub get_database_link {
    if (not defined $dblink) {
        if (not defined $dbpass) {
            if (defined $dbpassfile) {
                open(FH, $dbpassfile)
                    or report_fatal("failed to open $dbpassfile: $!");
                $dbpass = <FH>;
                chomp($dbpass);
                $dbpass =~ s/\A\s*//;
                $dbpass =~ s/\s*\Z//;
                close(FH);
            }
        }

        my $dsn = "DBI:mysql:database=$dbname;host=$dbhost";
        $dblink = DBI->connect($dsn, $dbuser, $dbpass) or die(DBI::errstr);
    }

    return($dblink);
}


# !!! FIXME: I have a feeling this a perfect case of You're Doing It Wrong.
sub add_static_id {
    my ($table, $field, $id) = @_;
    # !!! FIXME: mutex this? Make sure two threads don't add same thing twice.

    my $link = get_database_link();
    $id = $link->quote($id);
    my $sql = "insert into $table ($field) values ($id)";
    if (not defined $link->do($sql)) {
        die("couldn't insert static id: $link->errstr");
    }

    $sql = 'select id from $table where ($field=$id) limit 1';
    my $sth = $link->prepare($sql);
    $sth->execute() or die "can't execute the query: $sth->errstr";
    my @row = $sth->fetchrow_array();
    my $retval = $row[0];
    $sth->finish();
    return $retval;
}


sub poke_bugtracker {
    my ($stacksha1, $stack, $cmdargsref) = @_;
    my $processed_text = '';

    my $link = get_database_link();
    my $sql = 'select id, trackerid from bugtracker_posts where (stack_sha1=$stacksha1) limit 1';
    my $sth = $link->prepare($sql);
    $sth->execute() or die "can't execute the query: $sth->errstr";
    my @row = $sth->fetchrow_array();
    my $newbug = not @row;
    my $id = $newbug ? 0 : $row[0];
    my $trackerid = $newbug ? 0 : $row[1];
    $sth->finish();


    my $report;
    if ($newbug) {
        $report = "A new crash, with the following callstack:\n\n$stack\n\n\n";
    } else {
        $report = "Another report with same callstack.\n\n";
    }

    foreach (keys $$cmdargsref) {
        $report .= $_ . ": " . $$cmdargsref[$_] . "\n";
    }
}


sub handle_unprocessed_report {
    my $id = shift;
    my $text = shift;

    my $line = 0;
    my $appname = undef;
    my $appver = undef;
    my $platname = undef;
    my $platver = undef;
    my $cpuname = undef;
    my $signal = undef;
    my $uptime = undef;
    my $crashtime = undef;
    my $lastcmd = undef;
    my %cmdargs;
    my %objs;
    my @callstack;
    my %etcs;
    my $etckey;
    my %seen;

    my @lines = split(/(\r\n|\r|\n)/, $text);
    foreach (@lines) {
        $lines++;

        my $multiple_okay = 0;
        s/\A\s+//;
        s/\s+\Z//;
        next if /\A\#/;  # skip comments.
        next if ($_ eq '');  # skip blank lines.

        # !!! FIXME: if line is > 512 chars, consider it bogus?

        my ($cmd, $args) = /\A(.*?)\s+(.*?)\Z/;

        if ((not defined $cmd) or ($cmd eq '')) {
            $bogus = 'corrupt line?';
        } elsif ($seen{'END'}) {
            $bogus = "END command in middle of report";
        } elsif ($cmd eq 'MOJOCRASH') {
            # do nothing right now.
        } elsif ($cmd eq 'CRASHLOG_VERSION') {
            $bogus = 'unrecognized crashlog version';
        } elsif ($cmd eq 'APPLICATION_NAME') {
            $appname = $apps{$args};
            $bogus = 'unknown application' if (not defined $appname);
        } elsif ($cmd eq 'APPLICATION_VERSION') {
            $appver = $appvers{$args};
            $bogus = 'unknown application version' if (not defined $appver);
        } elsif ($cmd eq 'PLATFORM_NAME') {
            $platname = $plats{$args};
            $bogus = 'unknown platform type' if (not defined $platname);
        } elsif ($cmd eq 'PLATFORM_VERSION') {
            $platver = $platvers{$args};
            if (not defined $platver) {
                # these change in the field a lot.
                $platver=add_static_id('platformversions', 'version', $args);
                $platvers{$args} = $platver;
            }
        } elsif ($cmd eq 'CPUARCH_NAME') {
            $cpuname = $cpus{$args};
            $bogus = 'unknown cpu type' if (not defined $cpuname);
        } elsif ($cmd eq 'CRASH_SIGNAL') {
            $signal = $args;
            $bogus = 'signal must be an integer' if (not $args =~ /\A\d+\Z/);
        } elsif ($cmd eq 'APPLICATION_UPTIME') {
            $uptime = $args;
            $bogus = 'uptime must be an integer' if (not $args =~ /\A\d+\Z/);
        } elsif ($cmd eq 'CRASH_TIME') {
            $crashtime = $args;
            $bogus = 'crashtime must be an integer' if (not $args =~ /\A\d+\Z/);
            $args = convert_to_datestring($crashtime);
        } elsif ($cmd eq 'END') {
            $bogus = 'END command with arguments' if ($args ne '');
        } elsif ($cmd eq 'OBJECT') {
            $multiple_okay = 1;
            if (not $seen{'CALLSTACK'}) {
                $bogus = 'OBJECT after CALLSTACK';
            } elsif (not $cmd =~ /\A(.*?)\/(\d+)\/(\d+)\Z/) {
                $bogus = 'invalid OBJECT format';
            } else {
                my ($obj, $addr, $len) = ( $1, $2, $3 );
                if (defined $objs{$obj}) {
                    $bogus = 'same OBJECT more than once';
                } else {
                    $objs{$obj} = ( $addr, $len );
                }
            }
        } elsif ($cmd eq 'CALLSTACK') {
            $multiple_okay = 1;
            if (not $seen{'OBJECT'}) {
                $bogus = 'CALLSTACK before OBJECT';
            } elsif (not $args =~ /\A\d+\Z/) {
                $bogus = 'callstack must be an integer';
            } else {
                unshift @callstack, $args;  # can't push; it's reverse order.
            }
        } elsif ($cmd eq 'ETC_KEY') {
            if ($etcs{$args}) {
                $bogus = 'duplicate ETC_KEY';
            } else {
                $etckey = $args;
            }
        } elsif ($cmd eq 'ETC_VALUE') {
            if ($lastcmd ne 'ETC_KEY') {
                $bogus = 'ETC_VALUE not following ETC_KEY';
            } else {
                $etcs{$etckey} = $args;
                $etckey = undef;
            }
        } else {
            $bogus = 'unknown command';
        }

        if ((defined $etckey) and ($lastcmd ne 'ETC_KEY')) {
            $bogus = 'ETC_KEY not followed by ETC_VALUE';
        } elsif (($seen{$cmd}) && (not $multiple_okay)) {
            $bogus = "multiple instances of '$cmd' command";
        }

        last if (defined $bogus);  # something failed, so stop.

        $cmdargs{$cmd} = $args;
        $lastcmd = $cmd;
        $seen{$cmd} = 1;
    }

    # Make sure we got all the data we wanted.
    if (not defined $bogus) {
        # !!! FIXME: make this static.
        my @required_cmds = qw(
            END MOJOCRASH CRASHLOG_VERSION APPLICATION_NAME APPLICATION_VERSION
            PLATFORM_NAME CPUARCH_NAME PLATFORM_VERSION CRASH_SIGNAL
            APPLICATION_UPTIME CRASH_TIME OBJECT CALLSTACK
        );

        foreach (@required_cmds) {
            if (not $seen{$_}) {
                $bogus = "missing required command '$_'";
                last;
            }
        }
    }

    my $callstack_sha1 = undef;
    my $processed_text = undef;
    if (not defined $bogus) {
        # okay, we're parsed now. Now it's time to process the information.
        #  first, we need to convert the stack into source files and line
        #  numbers.
        my $processed_callstack = convert_callstack(\@callstack, \%objs);
        $callstack_sha1 = sha1($processed_callstack);
        $processed_text = poke_bugtracker($callstack_sha1, $processed_callstack, \@cmdargs);
    }

    my $link = get_database_link();
    my $sql = undef;

    if (defined $bogus) {  # FAIL!
        # In theory, there are a finite number of these, all listed in the
        #  code, but it's easier to have this programatically update as things
        #  change.
        $bogusid = $boguses{$bogus};
        if (not defined $bogusid) {
            $bogusid = add_static_id('bogus_reasons', 'reason', $bogus);
            $boguses{$bogus} = $bogusid;
        }
        $sql = "update reports set status=-1, bogus_line=$line," .
               " bogus_reason=$bogusid where id=$id";
    } else {
        $sql = "update reports set status=1, s;lkjfs;ldkfj where id=$id";
    }
}

sub load_static_id_hash {
    my ($hashref, $table, $field) = @_;
    my $link = get_database_link();
    my $sql = 'select id, $field from $table';
    my $sth = $link->prepare($sql);
    $sth->execute() or die "can't execute the query: $sth->errstr";
    $$hashref = ();
    while (my @row = $sth->fetchrow_array()) {
        $$hashref{$row[1]} = $row[0];
    }
    $sth->finish();
}

sub load_static_ids {
    load_static_id_hash(\%apps, 'apps', 'name');
    load_static_id_hash(\%appvers, 'appversions', 'version');
    load_static_id_hash(\%plats, 'platforms', 'name');
    load_static_id_hash(\%platvers, 'platformversions', 'version');
    load_static_id_hash(\%cpus, 'cpuarchs', 'name');
}

sub run_unprocessed {
    load_static_ids();
    my $link = get_database_link();
    my $sql = 'select id, unprocessed_text from reports where (status=0)';
    my $sth = $link->prepare($sql);
    $sth->execute() or die "can't execute the query: $sth->errstr";
    while (my @row = $sth->fetchrow_array()) {
        # !!! FIXME: push to a queue that worker threads pull from.
        handle_unprocessed_report($row[0], $row[1]);
    }
    $sth->finish();
}

# mainline ...
run_unprocessed();
$dblink->disconnect() if defined $dblink;

# end of process_reports.pl ...

