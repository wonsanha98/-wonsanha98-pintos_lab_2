#include "filesys/file.h"
#include <debug.h>
#include "filesys/inode.h"
#include "threads/malloc.h"

/* An open file. */
struct file {
	struct inode *inode;        /* File's inode. */
	off_t pos;                  /* 현재 위치 */
	bool deny_write;            /* file_deny_write() 함수가 호출 되었는지, Has file_deny_write() been called? */
};

/* 주어진 INODE에 대해 파일을 열고, 해당 INODE의 소유권을 가져온 후 새 파일을 반환한다.
할당에 실패하거나 INODE가 null인 경우 null 포인터를 반환한다. */
struct file *
file_open (struct inode *inode) {
	struct file *file = calloc (1, sizeof *file);
	if (inode != NULL && file != NULL) {
		file->inode = inode;
		file->pos = 0;
		file->deny_write = false;
		return file;
	} else {
		inode_close (inode);
		free (file);
		return NULL;
	}
}

/* Opens and returns a new file for the same inode as FILE.
 * Returns a null pointer if unsuccessful. */
struct file *
file_reopen (struct file *file) {
	return file_open (inode_reopen (file->inode));
}

/*
 * 파일 객체를 속성까지 포함하여 복제하며,
 * FILE과 동일한 inode를 참조하는 새로운 파일을 반환합니다.
 * 복제에 실패하면 null 포인터를 반환합니다.
 */
struct file *
file_duplicate (struct file *file) {
	struct file *nfile = file_open (inode_reopen (file->inode));
	if (nfile) {
		nfile->pos = file->pos;
		if (file->deny_write)
			file_deny_write (nfile);
	}
	return nfile;
}

/* Closes FILE. */
void
file_close (struct file *file) {
	if (file != NULL) {
		file_allow_write (file);
		inode_close (file->inode);
		free (file);
	}
}

/* Returns the inode encapsulated by FILE. */
struct inode *
file_get_inode (struct file *file) {
	return file->inode;
}

/* 파일의 현재 위치에서 시작하여 FILE로부터 BUFFER로 SIZE 바이트를 읽어옵니다.
실제로 읽힌 바이트 수를 반환하며, 파일의 끝(EOF)에 도달한 경우 SIZE보다 작을 수 있습니다.
읽힌 바이트 수만큼 FILE의 위치를 앞으로 이동시킵니다. */
off_t
file_read (struct file *file, void *buffer, off_t size) {
	off_t bytes_read = inode_read_at (file->inode, buffer, size, file->pos);
	file->pos += bytes_read;
	return bytes_read;
}

/* Reads SIZE bytes from FILE into BUFFER,
 * starting at offset FILE_OFS in the file.
 * Returns the number of bytes actually read,
 * which may be less than SIZE if end of file is reached.
 * The file's current position is unaffected. */
off_t
file_read_at (struct file *file, void *buffer, off_t size, off_t file_ofs) {
	return inode_read_at (file->inode, buffer, size, file_ofs);
}

/* BUFFER로부터 SIZE 바이트를 FILE의 현재 위치에서부터 파일에 기록한다.
실제로 기록된 바이트 수를 반환하며, 파일 끝에 도달한 경우 SIZE보다 작을 수 있다.
(일반적으로 이 경우 파일을 확장하지만, 파일 확장은 아직 구현되지 않았다.)
파일 포인터는 읽은 바이트 수만큼 전진한다. */
off_t
file_write (struct file *file, const void *buffer, off_t size) {
	off_t bytes_written = inode_write_at (file->inode, buffer, size, file->pos);
	file->pos += bytes_written;
	return bytes_written;
}

/* Writes SIZE bytes from BUFFER into FILE,
 * starting at offset FILE_OFS in the file.
 * Returns the number of bytes actually written,
 * which may be less than SIZE if end of file is reached.
 * (Normally we'd grow the file in that case, but file growth is
 * not yet implemented.)
 * The file's current position is unaffected. */
off_t
file_write_at (struct file *file, const void *buffer, off_t size,
		off_t file_ofs) {
	return inode_write_at (file->inode, buffer, size, file_ofs);
}

/* Prevents write operations on FILE's underlying inode
 * until file_allow_write() is called or FILE is closed. */
void
file_deny_write (struct file *file) {
	ASSERT (file != NULL);
	if (!file->deny_write) {
		file->deny_write = true;
		inode_deny_write (file->inode);
	}
}

/* Re-enables write operations on FILE's underlying inode.
 * (Writes might still be denied by some other file that has the
 * same inode open.) */
void
file_allow_write (struct file *file) {
	ASSERT (file != NULL);
	if (file->deny_write) {
		file->deny_write = false;
		inode_allow_write (file->inode);
	}
}

/* FILE의 크기를 바이트 단위로 반환한다. */
off_t
file_length (struct file *file) {
	ASSERT (file != NULL);
	return inode_length (file->inode);
}

/* Sets the current position in FILE to NEW_POS bytes from the
 * start of the file. */
void
file_seek (struct file *file, off_t new_pos) {
	ASSERT (file != NULL);
	ASSERT (new_pos >= 0);
	file->pos = new_pos;
}

/* 파일의 시작 지점으로부터의 바이트 오프셋으로, FILE에서의 현재 위치를 반환한다. */
off_t
file_tell (struct file *file) {
	ASSERT (file != NULL);
	return file->pos;
}
