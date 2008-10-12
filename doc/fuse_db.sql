CREATE TABLE inodes (ino serial4 primary key, type char(3), mode int2, size int8);
CREATE TABLE file_tree ("offset" serial4 primary key, name varchar(256) NOT NULL, parent int4 references inodes(ino) NOT NULL, inode int4 references inodes(ino) NOT NULL);

INSERT INTO inodes VALUES (1, 'DIR', 365, 0);
INSERT INTO inodes VALUES (2, 'REG', 292, 0);
INSERT INTO file_tree (name, parent, inode) VALUES ('foo', 1, 2);

ALTER TABLE file_tree ALTER COLUMN name DROP NOT NULL;
ALTER TABLE file_tree ALTER COLUMN parent DROP NOT NULL;
INSERT INTO file_tree (name, parent, inode) VALUES (NULL, NULL, 1);

