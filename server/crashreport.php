<?php

$max_file_size = 1024 * 256;

$dblink = NULL;
$dbhost = 'localhost';
$dbuser = 'root';
$dbpass = '';
$dbname = 'mojocrash';

function close_database_link()
{
    global $dblink;
    if ($dblink != NULL)
    {
        mysql_close($dblink);
        $dblink = NULL;
    } // if
} // close_database_link


function fail($errstr)
{
    header('HTTP/1.1 503 Service Unavailable');
    echo("\n\n$errstr\n\n");
    close_database_link();
    exit(0);
} // fail


function get_database_link()
{
    global $dblink;

    if ($dblink == NULL)
    {
        global $dbhost, $dbuser, $dbpass, $dbname;
        $dblink = mysql_connect($dbhost, $dbuser, $dbpass);
        if (!$dblink)
            fail("Failed to open database link: " . mysql_error());

        if (!mysql_select_db($dbname))
            fail("Failed to select database: " . mysql_error());
    } // if

    return($dblink);
} // get_database_link


function database_escape_string($str)
{
    return("'" . mysql_escape_string($str) . "'");
} // db_escape_string


function process_report()
{
    global $max_file_size;

    $ua = 'mojocrash/';
    if (strncmp($_SERVER['HTTP_USER_AGENT'], $ua, strlen($ua)) != 0)
        return;  // ignore things that definitely aren't MojoCrash clients.

    foreach ($_FILES as $key => $val) { echo "$key<br/>\n"; }

    $file = $_FILES['crash'];
    if (!isset($file))
        fail('File not seen');

    $len = $file['size'];
    $fname = $file['tmp_name'];
    if ($file['error'] != UPLOAD_ERR_OK)
        fail('upload error');
    else if (($len <= 0) || ($len > $max_file_size))
        fail('bad file size');
    else if (!is_uploaded_file($fname))
        fail('bad file');
    else if (($str=file_get_contents($fname, 0, NULL, 0, $len)) === FALSE)
        fail('read error');
    else if (($dblink=get_database_link()) == NULL)
        fail('database connection failure');

    $str = database_escape_string($str);
    $ipaddr = ip2long($_SERVER['REMOTE_ADDR']);
    $sql = "insert into reports (ipaddr, postdate, unprocessed_text) " .
           " values ($ipaddr, NOW(), $str)";
    $str = '';
    if (mysql_query($sql, $dblink) == false)
        fail('database write failure: ' . mysql_error());
} // process_report


// Mainline ...
header('Content-Type: text/plain; charset=utf-8');
process_report();
close_database_link();
echo("\n\nThanks!\n\n");

?>
