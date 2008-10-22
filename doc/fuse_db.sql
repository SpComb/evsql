
DROP TABLE IF EXISTS file_tree;
DROP TABLE IF EXISTS inodes;
DROP SEQUENCE IF EXISTS ino_seq;

CREATE SEQUENCE ino_seq START 64;

CREATE TABLE inodes (
    ino int4 primary key DEFAULT nextval('ino_seq'::regclass), 
    type char(3) NOT NULL, 
    mode int2 NOT NULL, 
    data oid,
    link_path varchar(512)
);

CREATE TABLE file_tree (
    "offset" serial4 primary key, 
    name varchar(256), 
    ino int4 references inodes(ino) NOT NULL,
    ino_dir int4 references inodes(ino),
    parent int4,

    CONSTRAINT file_tree_uniq_direntry UNIQUE (parent, name),
    CONSTRAINT file_tree_uniq_dir_ino UNIQUE (ino_dir),
    CONSTRAINT file_tree_exist_parent FOREIGN KEY (parent) REFERENCES file_tree(ino_dir)
);

INSERT INTO inodes (ino, type, mode, data) VALUES 
    (1, 'DIR', 365, NULL),
    (2, 'REG', 292, lo_create(0));

INSERT INTO file_tree (name, parent, ino, ino_dir) VALUES 
    (NULL,  NULL,   1,  1       ),
    ('foo', 1,      2,  NULL    );

CREATE OR REPLACE FUNCTION lo_size (oid) RETURNS int4 LANGUAGE SQL STABLE RETURNS NULL ON NULL INPUT AS 'select lo_lseek(lo_open($1, 262144), 0, 2);';
CREATE OR REPLACE FUNCTION lo_pread (IN fd int4, IN len int4, IN "off" int4) RETURNS bytea LANGUAGE SQL STRICT AS 'select lo_lseek($1, $3, 0); select loread($1, $2);';
CREATE OR REPLACE FUNCTION lo_pwrite (IN fd int4, IN buf bytea, IN "off" int4) RETURNS int4 LANGUAGE SQL STRICT AS 'select lo_lseek($1, $3, 0); select lowrite($1, $2);';
CREATE OR REPLACE FUNCTION lo_otruncate (IN oid, IN len int4) RETURNS oid LANGUAGE SQL STRICT AS 'select lo_truncate(lo_open($1, 393216), $2); select $1;';

CREATE OR REPLACE FUNCTION dbfs_size (type char, oid, link varchar) RETURNS int4 LANGUAGE SQL STABLE AS $$
    SELECT CASE $1 
        WHEN 'LNK' THEN char_length($3) 
        WHEN 'REG' THEN lo_size($2) 
        ELSE 0 
    END;
$$;

CREATE OR REPLACE FUNCTION dbfs_link (
    IN ino int4, IN new_parent int4, IN new_name varchar, 
    OUT ino int4, OUT type char(3), OUT mode int2, OUT size int4, OUT nlink int8
) LANGUAGE SQL VOLATILE AS $$
    INSERT INTO file_tree (name, ino, parent) VALUES ($3, $1, $2);
    SELECT ino, type, mode, dbfs_size(type, data, link_path) AS size, (SELECT COUNT(*) FROM inodes i LEFT JOIN file_tree ft ON (i.ino = ft.ino) WHERE i.ino = inodes.ino) AS nlink
     FROM inodes WHERE ino = $1;
$$;
