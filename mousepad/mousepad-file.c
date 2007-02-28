/* $Id$ */
/*
 * Copyright (c) 2007 Nick Schermer <nick@xfce.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef HAVE_MEMORY_H
#include <memory.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_TIME_H
#include <time.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <mousepad/mousepad-private.h>
#include <mousepad/mousepad-file.h>



gboolean
mousepad_file_get_externally_modified (const gchar  *filename,
                                       gint          mtime,
                                       GError      **error)
{
  gint         fd;
  struct stat  statb;
  gboolean     modified = FALSE;

  _mousepad_return_val_if_fail (filename != NULL, FALSE);
  _mousepad_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  /* open the file for reading */
  fd = open (filename, O_RDONLY);
  if (G_UNLIKELY (fd < 0))
    {
      g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_IO,
                   _("Failed to open \"%s\" for reading"), filename);
      return FALSE;
    }

  /* check if the file has been modified */
  if (G_LIKELY (fstat (fd, &statb) == 0))
    modified = (mtime > 0 && statb.st_mtime != mtime);

  /* close the file */
  close (fd);

  return modified;
}



gboolean
mousepad_file_save_data (const gchar  *filename,
                         const gchar  *data,
                         gsize         bytes,
                         gint         *new_mtime,
                         GError      **error)
{
  gint         fd;
  struct stat  statb;
  gboolean     succeed = FALSE;
  gint         n, m, l;

  _mousepad_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  _mousepad_return_val_if_fail (filename != NULL, FALSE);
  _mousepad_return_val_if_fail (data != NULL, FALSE);

  /* try to create the file (this failes if it already exists) */
  fd = open (filename, O_CREAT | O_EXCL | O_WRONLY, 0666);
  if (fd < 0)
    {
      if (G_LIKELY (errno == EEXIST))
        {
          /* try to open an existing file for reading */
          fd = open (filename, O_RDWR);
          if (G_UNLIKELY (fd < 0))
            {
              g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_IO,
                           _("Failed to open \"%s\" for writing"), filename);
              return FALSE;
             }
        }
      else
        {
          /* failed to create a file */
          g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
                       _("Failed to create new file \"%s\""), filename);
          return FALSE;
        }
    }

  /* get the file information */
  if (G_UNLIKELY (fstat (fd, &statb) != 0))
    goto failed;

  /* an extra check if we are allowed to write */
  if (G_UNLIKELY ((statb.st_mode & 00222) == 0 && access (filename, W_OK) != 0))
    {
      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
                   _("You don't have permission to write to \"%s\""), filename);
      goto failed;
    }

  /* make sure the document is empty and we're writing at the beginning of the file */
  if (G_LIKELY (lseek (fd, 0, SEEK_SET) != -1))
    if (G_UNLIKELY (ftruncate (fd, 0) != 0))
      {
        g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
                     _("Failed to truncate \"%s\" before writing"), filename);
        goto failed;
      }

  /* write the content to the file */
  for (m = 0, n = bytes; m < n; )
    {
      /* write the data piece by piece */
      l = write (fd, data + m, n - m);

      if (G_UNLIKELY (l < 0))
        {
          /* just try again on EAGAIN/EINTR */
          if (G_LIKELY (errno != EAGAIN && errno != EINTR))
            {
              g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
                           _("Failed to write data to \"%s\""), filename);
              goto failed;
            }
        }
      else
        {
          /* advance the offset */
          m += l;
        }
    }

  /* re-stat the mtime */
  if (G_UNLIKELY (fstat (fd, &statb) == 0))
    *new_mtime = statb.st_mtime;

  /* mm.. it seems everything worked fine */
  succeed = TRUE;

failed:
  /* close the file */
  close (fd);

  return succeed;
}



gboolean
mousepad_file_read_to_buffer (const gchar    *filename,
                              GtkTextBuffer  *buffer,
                              gint           *new_mtime,
                              gboolean       *readonly,
                              GError        **error)
{
  GtkTextIter    start_iter;
  struct stat    statb;
  gchar         *content;
  gint           fd;
  gboolean       succeed = FALSE;

  _mousepad_return_val_if_fail (GTK_IS_TEXT_BUFFER (buffer), FALSE);
  _mousepad_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  _mousepad_return_val_if_fail (filename != NULL, FALSE);

  /* get the start point in the buffer */
  gtk_text_buffer_get_start_iter (buffer, &start_iter);

  /* open the file for reading */
  fd = open (filename, O_RDONLY);
  if (G_UNLIKELY (fd < 0))
    {
      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
                   _("Failed to open \"%s\" for reading"), filename);
      return FALSE;
    }

  /* read the file information */
  if (G_UNLIKELY (fstat (fd, &statb) < 0))
    goto failed;

  /* extra check if we're allowed to read the file */
  if ((statb.st_mode & 00444) == 0 && access (filename, R_OK) != 0)
    {
      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
                   _("You are not allowed to read \"%s\""), filename);
      goto failed;
    }

  /* check if we're allowed to write the file */
  *readonly = !((statb.st_mode & 00222) != 0 && access (filename, W_OK) == 0);

#ifdef HAVE_MMAP
  /* try mmap() for files not larger than 8MB */
  content = (statb.st_size <= 8 * 1024 * 1024)
            ? (gchar *) mmap (NULL, statb.st_size, PROT_READ, MAP_SHARED, fd, 0)
            : MAP_FAILED;

  if (G_LIKELY (content != (gchar *) MAP_FAILED))
    {
#ifdef HAVE_POSIX_MADVISE
      /* tell the system that the data will be read sequentially */
      posix_madvise (content, statb.st_size, POSIX_MADV_SEQUENTIAL);
#endif

      /* insert the file content */
      gtk_text_buffer_insert (buffer, &start_iter, content, strlen (content));

      /* unmap */
      munmap (content, statb.st_size);
    }
  else
#endif /* !HAVE_MMAP */
    {
      /* load the file content by using the glib function */
      if (G_UNLIKELY (g_file_get_contents (filename, &content, NULL, error) == FALSE))
        goto failed;

      /* insert the file content */
      gtk_text_buffer_insert (buffer, &start_iter, content, strlen (content));

      /* cleanup */
      g_free (content);
    }

  /* set the mtime of the document */
  *new_mtime = statb.st_mtime;

  /* everything went ok */
  succeed = TRUE;

failed:
  /* close the file */
  close (fd);

  return succeed;
}



gboolean
mousepad_file_is_writable (const gchar  *filename,
                           GError      **error)
{
  struct stat statb;
  gint        fd;
  gboolean    writable = FALSE;

  _mousepad_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  _mousepad_return_val_if_fail (filename != NULL, FALSE);

  fd = open (filename, O_RDONLY);
  if (G_UNLIKELY (fd < 0))
    {
      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
                   _("Failed to open \"%s\" for reading"), filename);
      return FALSE;
    }

  /* read the file information */
  if (G_UNLIKELY (fstat (fd, &statb) < 0))
    goto failed;

  /* get the file information */
  if (G_UNLIKELY (fstat (fd, &statb) != 0))
    goto failed;

  /* an extra check if we are allowed to write */
  if ((statb.st_mode & 00222) != 0 && access (filename, W_OK) == 0)
    {
      writable = TRUE;
    }
  else
    {
      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
                   _("You don't have permission to write to \"%s\""), filename);
      goto failed;
    }


failed:
  /* close the file */
  close (fd);

  return writable;
}


