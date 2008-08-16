
drop database if exists mojocrash;
create database mojocrash;
use mojocrash;

create table apps (
    id int unsigned not null auto_increment,
    name varchar(64) not null,
    bugtracker_category int unsigned not null,
    primary key (id)
) character set utf8;

create table appversions (
    id int unsigned not null auto_increment,
    version varchar(64) not null,
    primary key (id)
) character set utf8;

create table platforms (
    id int unsigned not null auto_increment,
    name varchar(64) not null,
    primary key (id)
) character set utf8;

create table platformversions (
    id int unsigned not null auto_increment,
    version varchar(64) not null,
    primary key (id)
) character set utf8;

create table cpuarchs (
    id int unsigned not null auto_increment,
    name varchar(64) not null,
    primary key (id)
) character set utf8;

create table bogus_reasons (
    id int unsigned not null auto_increment,
    reason varchar(64) not null,
    primary key (id)
) character set utf8;

create table bugtracker_posts (
    id int unsigned not null auto_increment,
    guid char(40) not null,  -- SHA1 of appname . appver . processed_callstack
    trackerid int unsigned not null,
    primary key (id)
) character set utf8;

create table reports (
    -- these are filled in when the report is POSTed to the web server.
    id int unsigned not null auto_increment,
    ipaddr int unsigned not null,
    postdate datetime not null,
    unprocessed_text mediumtext not null,
    status int not null, -- 0==unprocessed, 1==processed, -1==bogus.

    -- everything below here isn't filled in until report is processed.
    bogus_line int unsigned,
    bogus_reason_id int unsigned,
    tracker_entry int unsigned,
    processed_text mediumtext,
    app_id int unsigned,
    platform_id int unsigned,
    platform_version_id int unsigned,
    cpuarch_id int unsigned,
    crashsignal int unsigned,
    crashtime datetime,
    uptime int unsigned,
    primary key (id)
) character set utf8;

-- end of schema.sql ...

