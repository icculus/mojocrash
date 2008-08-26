#!/usr/bin/perl -w -T

use warnings;
use strict;
use DBI;
use DateTime::Format::MySQL;
use DateTime::Format::Epoch::Unix;
use Digest::SHA1 qw(sha1_hex);

my $GTimeZone = 'EST';  # !!! FIXME: get this from the system?

my $dblink = undef;
my $dbhost = 'localhost';
my $dbuser = 'root';
my $dbpass = '';
my $dbname = 'mojocrash';

my %apps;
my %appvers;
my %plats;
my %platvers;
my %cpus;
my %boguses;

sub epochToMysql {
    my $epochtime = shift;
    my $dt = DateTime::Format::Epoch::Unix->parse_datetime($epochtime);
    $dt->set_time_zone($GTimeZone);
    return DateTime::Format::MySQL->format_datetime($dt);
}

# !!! FIXME: grep for 'die' and remove it. This needs to stay up in face of
# !!! FIXME:  temporary failures, and not take down other threads, too!

sub get_database_link {
    if (not defined $dblink) {
        if (not defined $dbpass) {
            return undef;
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
        die("couldn't insert static id: " . $link->errstr);
    }

    $sql = "select id from $table where ($field=$id) limit 1";
    my $sth = $link->prepare($sql);
    $sth->execute() or die "can't execute the query: " . $sth->errstr;
    my @row = $sth->fetchrow_array();
    my $retval = $row[0];
    $sth->finish();
    return $retval;
}


sub poke_bugzilla {
    my ($newbug, $report, $trackerid) = @_;
    if ($newbug) {
        # !!! FIXME: post to url, get $trackerid
        print "new bug to bugzilla\n";
        $trackerid = int(rand());
    } else {
        # !!! FIXME: post to other url
        print "update bug in bugzilla\n";
    }
    
    return $trackerid;
}


sub poke_bugtracker {
    my ($id, $guid, $stack, $cmdargsref, $etcsref) = @_;
    my $processed_text = '';

    my $link = get_database_link();
    my $sql = "select id, trackerid from bugtracker_posts where (guid=$guid) limit 1";
    my $sth = $link->prepare($sql);
    $sth->execute() or die "can't execute the query: " . $sth->errstr;
    my @row = $sth->fetchrow_array();
    my $newbug = not @row;
    my $postid = $newbug ? 0 : $row[0];
    my $trackerid = $newbug ? 0 : $row[1];
    $sth->finish();

    my $report;
    if ($newbug) {
        $report = "A new crash, with the following callstack:\n\n$stack\n\n\n";
    } else {
        $report = "Another report with same callstack.\n\n";
    }

    $report .= "MojoCrash report #$id, post #$postid, guid $guid\n";

    foreach (keys %$$cmdargsref) {
        $report .= $_ . ': ' . $$cmdargsref[$_] . "\n";
    }

    foreach (keys %$$etcsref) {
        $report .= "\nETC_KEY: $_\n" . 'ETC_VALUE: ' . $$etcsref[$_] . "\n";
    }

    $report .= "\n\n";

    # !!! FIXME: handle stuff other than Bugzilla.
    $trackerid = poke_bugzilla($newbug, $report, $trackerid);
        
    return ($report, $trackerid);
}


sub handle_unprocessed_report {
    my $id = shift;
    my $text = shift;

    my $line = 0;
    my $bogus = undef;
    my $appname = undef;
    my $appver = undef;
    my $platname = undef;
    my $platver = undef;
    my $cpuname = undef;
    my $signal = undef;
    my $uptime = undef;
    my $crashtime = undef;
    my $etckey = undef;
    my $lastcmd = '';
    my %cmdargs = ();
    my %objs = ();
    my @callstack = ();
    my %etcs = ();
    my %seen = ();

    # !!! FIXME: race condition, if two NEW reports match in two threads.
    my $checksum = sha1_hex($text);
    my $link = get_database_link();
    my $sql = "select id from reports where checksum='$checksum' limit 1";
    my $sth = $link->prepare($sql);
    $sth->execute() or die "can't execute the query: " . $sth->errstr;
    my @row = $sth->fetchrow_array();
    $bogus = 'Duplicate bug report' if (@row);
    $sth->finish();
    @row = undef;

    my @textlines = ();
    if (not defined $bogus) {
        @textlines = split(/(\r\n|\r|\n)/, $text);
    }

    foreach (@textlines) {
        $line++;

        my $multiple_okay = 0;
        s/\A\s+//;
        s/\s+\Z//;
        next if /\A\#/;  # skip comments.
        next if ($_ eq '');  # skip blank lines.

        # !!! FIXME: if line is > 512 chars, consider it bogus?

        my $cmd = '';
        my $args = '';
        if ($_ eq 'END') {
            $cmd = $_;
        } else {
            ($cmd, $args) = /\A(.*?)\s+(.*?)\Z/;
        }

        if ((not defined $cmd) or ($cmd eq '')) {
            $bogus = 'corrupt line?';
        } elsif ($seen{'END'}) {
            $bogus = "END command in middle of report";
        } elsif ($cmd eq 'MOJOCRASH') {
            # do nothing right now.
        } elsif ($cmd eq 'CRASHLOG_VERSION') {
            $bogus = 'unrecognized crashlog version' if ($args != 1);
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
            $bogus = 'crashtime must be an integer' if (not $args =~ /\A\d+\Z/);
            $crashtime = epochToMysql($args);
            $args = $crashtime;
        } elsif ($cmd eq 'END') {
            $bogus = 'END command with arguments' if ($args ne '');
        } elsif ($cmd eq 'OBJECT') {
            $multiple_okay = 1;
            if ($seen{'CALLSTACK'}) {
                $bogus = 'OBJECT after CALLSTACK';
            } elsif (not $args =~ /\A(.*?)\/(\d+)\/(\d+)\Z/) {
                $bogus = 'invalid OBJECT format';
            } else {
                my ($obj, $addr, $len) = ( $1, $2, $3 );
                if (defined $objs{$obj}) {
                    $bogus = 'same OBJECT more than once';
                } else {
                    $objs{$obj} = [ $addr, $len ];
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
            $multiple_okay = 1;
            if ($etcs{$args}) {
                $bogus = 'duplicate ETC_KEY';
            } else {
                $etckey = $args;
            }
        } elsif ($cmd eq 'ETC_VALUE') {
            $multiple_okay = 1;
            if ($lastcmd ne 'ETC_KEY') {
                $bogus = 'ETC_VALUE not following ETC_KEY';
            } else {
                $etcs{$etckey} = $args;
                $etckey = undef;
            }
        } else {
            $bogus = 'unknown command';
        }

        if ((defined $etckey) && ($lastcmd ne 'ETC_KEY')) {
            $bogus = 'ETC_KEY not followed by ETC_VALUE';
        } elsif (($seen{$cmd}) && (not $multiple_okay)) {
            $bogus = "multiple instances of '$cmd' command";
        }

        last if (defined $bogus);  # something failed, so stop.

        $cmdargs{$cmd} = $args if (!$multiple_okay);
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

    my $guid = undef;
    my $processed_text = undef;
    my $postid = undef;
    if (not defined $bogus) {
        # okay, we're parsed now. Now it's time to process the information.
        #  first, we need to convert the stack into source files and line
        #  numbers.
        my $processed_callstack = convert_callstack(\@callstack, \%objs);
        $guid = sha1_hex($appname . $appver . $processed_callstack);
        ($processed_text, $postid) = poke_bugtracker($guid, $processed_callstack, \%cmdargs, \%etcs);
    }

    if (defined $bogus) {  # FAIL!
        # In theory, there are a finite number of these, all listed in the
        #  code, but it's easier to have this programatically update as things
        #  change.
        my $bogusid = $boguses{$bogus};
        if (not defined $bogusid) {
            $bogusid = add_static_id('bogus_reasons', 'reason', $bogus);
            $boguses{$bogus} = $bogusid;
        }
        $sql = "update reports set status=-1, bogus_line=$line," .
               " bogus_reason_id=$bogusid where id=$id";
    } else {
        $checksum = $link->quote($checksum);
        $processed_text = $link->quote($processed_text);
        $sql = "update reports set status=1, bugtracker_entry=$postid," .
               " checksum=$checksum, processed_text=$processed_text," .
               " app_id=$appname, platform_id=$platname," .
               " platform_version_id=$platver, cpuarch_id=$cpuname, " .
               " crashsignal=$signal, crashtime=$crashtime," .
               " uptime=$uptime where id=$id";
    }

    $link->do($sql);
}


sub load_static_id_hash {
    my ($hashref, $table, $field) = @_;
    my $link = get_database_link();
    my $sql = "select id, $field from $table";
    my $sth = $link->prepare($sql);
    $sth->execute() or die "can't execute the query: " . $sth->errstr;
    %$hashref = ();
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
    load_static_id_hash(\%boguses, 'bogus_reasons', 'reason');
}

sub run_unprocessed {
    load_static_ids();
    my $link = get_database_link();
    my $sql = 'select id, unprocessed_text from reports where (status=0)';
    my $sth = $link->prepare($sql);
    $sth->execute() or die "can't execute the query: " . $sth->errstr;
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

