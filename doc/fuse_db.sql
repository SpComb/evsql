CREATE TABLE inodes (
    ino serial4 primary key, 
    type char(3) NOT NULL, 
    mode int2 NOT NULL, 
    data oid
);

CREATE TABLE file_tree (
    "offset" serial4 primary key, 
    name varchar(256), 
    parent int4 references inodes(ino), 
    inode int4 references inodes(ino) NOT NULL
);

INSERT INTO inodes VALUES 
    (1, 'DIR', 365, NULL),
    (2, 'REG', 292, lo_create(0));

INSERT INTO file_tree (name, parent, inode) VALUES 
    (NULL,  NULL,   1   ),
    ('foo', 1,      2   );

CREATE FUNCTION lo_size (oid) RETURNS int4 LANGUAGE SQL STABLE RETURNS NULL ON NULL INPUT AS 'select lo_lseek(lo_open($1, 262144), 0, 2);';
CREATE FUNCTION lo_pread (IN fd int4, IN len int4, IN "off" int4) RETURNS bytea LANGUAGE SQL STRICT AS 'select lo_lseek($1, $3, 0); select loread($1, $2);';
CREATE FUNCTION lo_pwrite (IN fd int4, IN buf bytea, IN "off" int4) RETURNS int4 LANGUAGE SQL STRICT AS 'select lo_lseek($1, $3, 0); select lowrite($1, $2);';
CREATE FUNCTION lo_otruncate (IN oid, IN len int4) RETURNS oid LANGUAGE SQL STRICT AS 'select lo_truncate(lo_open($1, 393216), $2); select $1;';

