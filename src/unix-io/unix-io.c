/*
 * UNIX Transport Plugin for Audacious
 * Copyright 2009 John Lindgren
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the documentation
 *    provided with the distribution.
 *
 * This software is provided "as is" and without any warranty, express or
 * implied. In no event shall the authors be liable for any damages arising from
 * the use of this software.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glib.h>

#include <audacious/plugin.h>

#define error(...) fprintf (stderr, "unix-io: " __VA_ARGS__)

#if 0
#define debug(...) error (__VA_ARGS__)
#else
#define debug(...)
#endif

static VFSFile * unix_fopen (const gchar * uri, const gchar * mode)
{
    VFSFile * file = NULL;
    gboolean update;
    mode_t mode_flag;
    gchar * filename;
    gint handle;

    debug ("fopen %s, mode = %s\n", uri, mode);

    update = (strchr (mode, '+') != NULL);

    switch (mode[0])
    {
      case 'r':
        mode_flag = update ? O_RDWR : O_RDONLY;
        break;
      case 'w':
        mode_flag = (update ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC;
        break;
      case 'a':
        mode_flag = (update ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND;
        break;
      default:
        return NULL;
    }

    filename = g_filename_from_uri (uri, NULL, NULL);

    if (filename == NULL)
        return NULL;

    if (mode_flag & O_CREAT)
        handle = open (filename, mode_flag, S_IRUSR | S_IWUSR | S_IRGRP |
         S_IROTH);
    else
        handle = open (filename, mode_flag);

    if (handle == -1)
    {
        error ("Cannot open %s: %s.\n", filename, strerror (errno));
        goto DONE;
    }

    fcntl (handle, F_SETFD, FD_CLOEXEC);

    file = g_malloc (sizeof * file);
    file->handle = GINT_TO_POINTER (handle);

DONE:
    g_free (filename);
    return file;
}

static gint unix_fclose (VFSFile * file)
{
    gint handle = GPOINTER_TO_INT (file->handle);
    gint result = 0;

    debug ("fclose\n");

    if (fsync (handle) == -1)
    {
        error ("fsync failed: %s.\n", strerror (errno));
        result = EOF;
    }

    close (handle);
    return result;
}

static size_t unix_fread (gpointer ptr, size_t size, size_t nitems, VFSFile *
 file)
{
    gint handle = GPOINTER_TO_INT (file->handle);
    gint goal = size * nitems;
    gint total = 0;

    debug ("fread %d x %d\n", size, nitems);

    while (total < goal)
    {
        gint readed = read (handle, ptr + total, goal - total);

        if (readed == -1)
        {
            error ("read failed: %s.\n", strerror (errno));
            break;
        }

        if (readed == 0)
            break;

        total += readed;
    }

    debug (" = %d\n", total);

    return (size > 0) ? total / size : 0;
}

static size_t unix_fwrite (gconstpointer ptr, size_t size, size_t nitems,
 VFSFile * file)
{
    gint handle = GPOINTER_TO_INT (file->handle);
    gint goal = size * nitems;
    gint total = 0;

    debug ("fwrite %d x %d\n", size, nitems);

    while (total < goal)
    {
        gint written = write (handle, ptr + total, goal - total);

        if (written == -1)
        {
            error ("write failed: %s.\n", strerror (errno));
            break;
        }

        total += written;
    }

    debug (" = %d\n", total);

    return (size > 0) ? total / size : 0;
}

static gint unix_fseek (VFSFile * file, glong offset, gint whence)
{
    gint handle = GPOINTER_TO_INT (file->handle);

    debug ("fseek %ld, whence = %d\n", offset, whence);

    if (lseek (handle, offset, whence) == -1)
    {
        error ("lseek failed: %s.\n", strerror (errno));
        return -1;
    }

    return 0;
}

static glong unix_ftell (VFSFile * file)
{
    gint handle = GPOINTER_TO_INT (file->handle);
    glong result = lseek (handle, 0, SEEK_CUR);

    if (result == -1)
        error ("lseek failed: %s.\n", strerror (errno));

    return result;
}

static gint unix_getc (VFSFile * file)
{
    guchar c;

    return (unix_fread (& c, 1, 1, file) == 1) ? c : EOF;
}

static gint unix_ungetc (gint c, VFSFile * file)
{
    return (unix_fseek (file, -1, SEEK_CUR) == 0) ? c : EOF;
}

static void unix_rewind (VFSFile * file)
{
    unix_fseek (file, 0, SEEK_SET);
}

static gboolean unix_feof (VFSFile * file)
{
    gint test = unix_getc (file);

    if (test == EOF)
        return TRUE;

    unix_ungetc (test, file);
    return FALSE;
}

static gboolean unix_truncate (VFSFile * file, glong length)
{
    gint handle = GPOINTER_TO_INT (file->handle);
    gint result = ftruncate (handle, length);

    if (result == -1)
    {
        error ("ftruncate failed: %s.\n", strerror (errno));
        return FALSE;
    }

    return TRUE;
}

static off_t unix_fsize (VFSFile * file)
{
    glong position, length;

    position = unix_ftell (file);

    if (position == -1)
        return -1;

    unix_fseek (file, 0, SEEK_END);
    length = unix_ftell (file);

    if (length == -1)
        return -1;

    unix_fseek (file, position, SEEK_SET);

    return length;
}

static VFSConstructor constructor =
{
    .uri_id = "file://",
    .vfs_fopen_impl = unix_fopen,
    .vfs_fclose_impl = unix_fclose,
    .vfs_fread_impl = unix_fread,
    .vfs_fwrite_impl = unix_fwrite,
    .vfs_getc_impl = unix_getc,
    .vfs_ungetc_impl = unix_ungetc,
    .vfs_fseek_impl = unix_fseek,
    .vfs_rewind_impl = unix_rewind,
    .vfs_ftell_impl = unix_ftell,
    .vfs_feof_impl = unix_feof,
    .vfs_truncate_impl = unix_truncate,
    .vfs_fsize_impl = unix_fsize,
    .vfs_get_metadata_impl = NULL,
};

static void unix_init (void)
{
    aud_vfs_register_transport (& constructor);
}

DECLARE_PLUGIN (unix_io, unix_init, NULL);