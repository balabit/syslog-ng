/*
 * Copyright (c) 2002-2018 Balabit
 * Copyright (c) 1998-2018 Balázs Scheidler
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */
#include "timeutils/scan-timestamp.h"
#include "timeutils/wallclocktime.h"
#include "str-format.h"
#include "timeutils/cache.h"

#include <ctype.h>
#include <string.h>

gboolean
scan_day_abbrev(const gchar **buf, gint *left, gint *wday)
{
  *wday = -1;

  if (*left < 3)
    return FALSE;

  switch (**buf)
    {
    case 'S':
      if (strncasecmp(*buf, "Sun", 3) == 0)
        *wday = 0;
      else if (strncasecmp(*buf, "Sat", 3) == 0)
        *wday = 6;
      else
        return FALSE;
      break;
    case 'M':
      if (strncasecmp(*buf, "Mon", 3) == 0)
        *wday = 1;
      else
        return FALSE;
      break;
    case 'T':
      if (strncasecmp(*buf, "Tue", 3) == 0)
        *wday = 2;
      else if (strncasecmp(*buf, "Thu", 3) == 0)
        *wday = 4;
      else
        return FALSE;
      break;
    case 'W':
      if (strncasecmp(*buf, "Wed", 3) == 0)
        *wday = 3;
      else
        return FALSE;
      break;
    case 'F':
      if (strncasecmp(*buf, "Fri", 3) == 0)
        *wday = 5;
      else
        return FALSE;
      break;
    default:
      return FALSE;
    }

  (*buf) += 3;
  (*left) -= 3;
  return TRUE;
}

gboolean
scan_month_abbrev(const gchar **buf, gint *left, gint *mon)
{
  *mon = -1;

  if (*left < 3)
    return FALSE;

  switch (**buf)
    {
    case 'J':
      if (strncasecmp(*buf, "Jan", 3) == 0)
        *mon = 0;
      else if (strncasecmp(*buf, "Jun", 3) == 0)
        *mon = 5;
      else if (strncasecmp(*buf, "Jul", 3) == 0)
        *mon = 6;
      else
        return FALSE;
      break;
    case 'F':
      if (strncasecmp(*buf, "Feb", 3) == 0)
        *mon = 1;
      else
        return FALSE;
      break;
    case 'M':
      if (strncasecmp(*buf, "Mar", 3) == 0)
        *mon = 2;
      else if (strncasecmp(*buf, "May", 3) == 0)
        *mon = 4;
      else
        return FALSE;
      break;
    case 'A':
      if (strncasecmp(*buf, "Apr", 3) == 0)
        *mon = 3;
      else if (strncasecmp(*buf, "Aug", 3) == 0)
        *mon = 7;
      else
        return FALSE;
      break;
    case 'S':
      if (strncasecmp(*buf, "Sep", 3) == 0)
        *mon = 8;
      else
        return FALSE;
      break;
    case 'O':
      if (strncasecmp(*buf, "Oct", 3) == 0)
        *mon = 9;
      else
        return FALSE;
      break;
    case 'N':
      if (strncasecmp(*buf, "Nov", 3) == 0)
        *mon = 10;
      else
        return FALSE;
      break;
    case 'D':
      if (strncasecmp(*buf, "Dec", 3) == 0)
        *mon = 11;
      else
        return FALSE;
      break;
    default:
      return FALSE;
    }

  (*buf) += 3;
  (*left) -= 3;
  return TRUE;
}

/*******************************************************************************
 * RFC 3164 timestamp, expected format: "MMM DD HH:MM:SS" ...
 *******************************************************************************/

static gboolean
__is_bsd_rfc_3164(const guchar *src, guint32 left)
{
  return left >= 15 && src[3] == ' ' && src[6] == ' ' && src[9] == ':' && src[12] == ':';
}

gboolean
scan_bsd_timestamp(const gchar **buf, gint *left, WallClockTime *wct)
{
  if (!scan_month_abbrev(buf, left, &wct->wct_mon) ||
      !scan_expect_char(buf, left, ' ') ||
      !scan_positive_int(buf, left, 2, &wct->wct_mday) ||
      !scan_expect_char(buf, left, ' ') ||
      !scan_positive_int(buf, left, 2, &wct->wct_hour) ||
      !scan_expect_char(buf, left, ':') ||
      !scan_positive_int(buf, left, 2, &wct->wct_min) ||
      !scan_expect_char(buf, left, ':') ||
      !scan_positive_int(buf, left, 2, &wct->wct_sec))
    return FALSE;
  return TRUE;
}

/*******************************************************************************
 * ISO timestamp as specified in RFC5424, expected format "YYYY-MM-DDTHH:MM:SS"
 *******************************************************************************/

static gboolean
__is_iso_stamp(const gchar *stamp, gint length)
{
  return (length >= 19
          && stamp[4] == '-'
          && stamp[7] == '-'
          && stamp[10] == 'T'
          && stamp[13] == ':'
          && stamp[16] == ':'
         );
}

gboolean
scan_iso_timestamp(const gchar **buf, gint *left, WallClockTime *wct)
{
  if (!scan_positive_int(buf, left, 4, &wct->wct_year) ||
      !scan_expect_char(buf, left, '-') ||
      !scan_positive_int(buf, left, 2, &wct->wct_mon) ||
      !scan_expect_char(buf, left, '-') ||
      !scan_positive_int(buf, left, 2, &wct->wct_mday) ||
      !scan_expect_char(buf, left, 'T') ||
      !scan_positive_int(buf, left, 2, &wct->wct_hour) ||
      !scan_expect_char(buf, left, ':') ||
      !scan_positive_int(buf, left, 2, &wct->wct_min) ||
      !scan_expect_char(buf, left, ':') ||
      !scan_positive_int(buf, left, 2, &wct->wct_sec))
    return FALSE;
  wct->wct_year -= 1900;
  wct->wct_mon -= 1;
  return TRUE;
}

/*******************************************************************************
 * Cisco modified RFC3164 timestamp, expected format:
 *    "MMM DD YYYY HH:MM:SS:"
 *    "MMM DD YYYY HH:MM:SS "
 *******************************************************************************/

static gboolean
__is_bsd_pix_or_asa(const guchar *src, guint32 left)
{
  return (left >= 21
          && src[3] == ' '
          && src[6] == ' '
          && src[11] == ' '
          && src[14] == ':'
          && src[17] == ':'
          && (src[20] == ':' || src[20] == ' ')
          && isdigit(src[7])
          && isdigit(src[8])
          && isdigit(src[9])
          && isdigit(src[10])
         );
}

gboolean
scan_pix_timestamp(const gchar **buf, gint *left, WallClockTime *wct)
{
  if (!scan_month_abbrev(buf, left, &wct->wct_mon) ||
      !scan_expect_char(buf, left, ' ') ||
      !scan_positive_int(buf, left, 2, &wct->wct_mday) ||
      !scan_expect_char(buf, left, ' ') ||
      !scan_positive_int(buf, left, 4, &wct->wct_year) ||
      !scan_expect_char(buf, left, ' ') ||
      !scan_positive_int(buf, left, 2, &wct->wct_hour) ||
      !scan_expect_char(buf, left, ':') ||
      !scan_positive_int(buf, left, 2, &wct->wct_min) ||
      !scan_expect_char(buf, left, ':') ||
      !scan_positive_int(buf, left, 2, &wct->wct_sec))
    return FALSE;
  wct->wct_year -= 1900;
  return TRUE;
}

/*******************************************************************************
 * LinkSys modified RFC3164 timestamp, expected format:
 *    "MMM DD HH:MM:SS YYYY "
 *******************************************************************************/
static gboolean
__is_bsd_linksys(const guchar *src, guint32 left)
{
  /* "MMM DD HH:MM:SS YYYY " */
  return (left >= 21
          && src[3] == ' '
          && src[6] == ' '
          && src[9] == ':'
          && src[12] == ':'
          && src[15] == ' '
          && isdigit(src[16])
          && isdigit(src[17])
          && isdigit(src[18])
          && isdigit(src[19])
          && isspace(src[20])
         );
}

gboolean
scan_linksys_timestamp(const gchar **buf, gint *left, WallClockTime *wct)
{
  /* LinkSys timestamp, expected format: MMM DD HH:MM:SS YYYY */

  if (!scan_month_abbrev(buf, left, &wct->wct_mon) ||
      !scan_expect_char(buf, left, ' ') ||
      !scan_positive_int(buf, left, 2, &wct->wct_mday) ||
      !scan_expect_char(buf, left, ' ') ||
      !scan_positive_int(buf, left, 2, &wct->wct_hour) ||
      !scan_expect_char(buf, left, ':') ||
      !scan_positive_int(buf, left, 2, &wct->wct_min) ||
      !scan_expect_char(buf, left, ':') ||
      !scan_positive_int(buf, left, 2, &wct->wct_sec) ||
      !scan_expect_char(buf, left, ' ') ||
      !scan_positive_int(buf, left, 4, &wct->wct_year))
    return FALSE;
  wct->wct_year -= 1900;
  return TRUE;
}

/*******************************************************************************
 * Parse ISO timestamp
 *******************************************************************************/

static guint32
__parse_usec(const guchar **data, gint *length)
{
  guint32 usec = 0;
  const guchar *src = *data;
  if (*length > 0 && *src == '.')
    {
      gulong frac = 0;
      gint div = 1;
      /* process second fractions */

      src++;
      (*length)--;
      while (*length > 0 && div < 10e5 && isdigit(*src))
        {
          frac = 10 * frac + (*src) - '0';
          div = div * 10;
          src++;
          (*length)--;
        }
      while (*length > 0 && isdigit(*src))
        {
          src++;
          (*length)--;
        }
      usec = frac * (1000000 / div);
    }
  *data = src;
  return usec;
}

static gboolean
__has_iso_timezone(const guchar *src, gint length)
{
  return (length >= 5) &&
         (*src == '+' || *src == '-') &&
         isdigit(*(src+1)) &&
         isdigit(*(src+2)) &&
         *(src+3) == ':' &&
         isdigit(*(src+4)) &&
         isdigit(*(src+5)) &&
         !isdigit(*(src+6));
}

static guint32
__parse_iso_timezone(const guchar **data, gint *length)
{
  gint hours, mins;
  const guchar *src = *data;
  guint32 tz = 0;
  /* timezone offset */
  gint sign = *src == '-' ? -1 : 1;

  hours = (*(src + 1) - '0') * 10 + *(src + 2) - '0';
  mins = (*(src + 4) - '0') * 10 + *(src + 5) - '0';
  tz = sign * (hours * 3600 + mins * 60);
  src += 6;
  (*length) -= 6;
  *data = src;
  return tz;
}


static gboolean
__parse_iso_stamp(WallClockTime *wct, const guchar **data, gint *length)
{
  /* RFC3339 timestamp, expected format: YYYY-MM-DDTHH:MM:SS[.frac]<+/->ZZ:ZZ */
  const guchar *src = *data;

  if (!scan_iso_timestamp((const gchar **) &src, length, wct))
    {
      return FALSE;
    }

  wct->wct_usec = __parse_usec(&src, length);

  if (*length > 0 && *src == 'Z')
    {
      /* Z is special, it means UTC */
      wct->wct_gmtoff = 0;
      src++;
      (*length)--;
    }
  else if (__has_iso_timezone(src, *length))
    {
      wct->wct_gmtoff = __parse_iso_timezone(&src, length);
    }
  else
    {
      wct->wct_gmtoff = -1;
    }

  *data = src;
  return TRUE;
}

/*******************************************************************************
 * Parse BSD timestamp
 *******************************************************************************/

static gboolean
__parse_bsd_timestamp(const guchar **data, gint *length, WallClockTime *wct)
{
  gint left = *length;
  const guchar *src = *data;

  if (__is_bsd_pix_or_asa(src, left))
    {
      if (!scan_pix_timestamp((const gchar **) &src, &left, wct))
        return FALSE;

      if (*src == ':')
        {
          src++;
          left--;
        }
    }
  else if (__is_bsd_linksys(src, left))
    {
      if (!scan_linksys_timestamp((const gchar **) &src, &left, wct))
        return FALSE;
    }
  else if (__is_bsd_rfc_3164(src, left))
    {
      if (!scan_bsd_timestamp((const gchar **) &src, &left, wct))
        return FALSE;

      wct->wct_usec = __parse_usec(&src, &left);

      wall_clock_time_guess_missing_year(wct);
    }
  else
    {
      return FALSE;
    }
  *data = src;
  *length = left;
  return TRUE;
}

gboolean
scan_rfc3164_timestamp(const guchar **data, gint *length, WallClockTime *wct)
{
  const guchar *src = *data;
  gint left = *length;

  /* If the next chars look like a date, then read them as a date. */
  if (__is_iso_stamp((const gchar *)src, left))
    {
      if (!__parse_iso_stamp(wct, &src, &left))
        return FALSE;
    }
  else
    {
      if (!__parse_bsd_timestamp(&src, &left, wct))
        return FALSE;
    }

  /* we might have a closing colon at the end of the timestamp, "Cisco" I am
   * looking at you, skip that as well, so we can reliably detect IPv6
   * addresses as hostnames, which would be using ":" as well. */

  if (left > 0 && *src == ':')
    {
      ++src;
      --left;
    }

  *data = src;
  *length = left;
  return TRUE;
}

gboolean
scan_rfc5424_timestamp(const guchar **data, gint *length, WallClockTime *wct)
{
  const guchar *src = *data;
  gint left = *length;

  if (!__parse_iso_stamp(wct, &src, &left))
    return FALSE;

  *data = src;
  *length = left;
  return TRUE;
}
