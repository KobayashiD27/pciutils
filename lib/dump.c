/*
 *	The PCI Library -- Reading of Bus Dumps
 *
 *	Copyright (c) 1997--2008 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL v2+.
 *
 *	SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>

#include "internal.h"

struct dump_data {
  int len, allocated;
  byte data[1];
};

static void
dump_config(struct pci_access *a)
{
  pci_define_param(a, "dump.name", "", "Name of the bus dump file to read from");
}

static int
dump_detect(struct pci_access *a)
{
  char *name = pci_get_param(a, "dump.name");
  return name && name[0];
}

static void
dump_alloc_data(struct pci_dev *dev, int len)
{
  struct dump_data *dd = pci_malloc(dev->access, sizeof(struct dump_data) + len - 1);
  dd->allocated = len;
  dd->len = 0;
  memset(dd->data, 0xff, len);
  dev->backend_data = dd;
}

static int
dump_validate(char *s, char *fmt)
{
  while (*fmt)
    {
      if (*fmt == '#' ? !isxdigit(*s) : *fmt != *s)
	return 0;
      fmt++, s++;
    }
  return 1;
}

static void
dump_init(struct pci_access *a)
{
  char *name = pci_get_param(a, "dump.name");
  FILE *f;
  char buf[256];
  struct pci_dev *dev = NULL;
  int len, mn, bn, dn, fn, i, j;

  if (!name)
    a->error("dump: File name not given.");
  if (!(f = fopen(name, "r")))
    a->error("dump: Cannot open %s: %s", name, strerror(errno));
  while (fgets(buf, sizeof(buf)-1, f))
    {
      char *z = strchr(buf, '\n');
      if (!z)
	{
	  fclose(f);
	  a->error("dump: line too long or unterminated");
	}
      *z-- = 0;
      if (z >= buf && *z == '\r')
	*z-- = 0;
      len = z - buf + 1;
      mn = 0;
      if (dump_validate(buf, "##:##.# ") && sscanf(buf, "%x:%x.%d", &bn, &dn, &fn) == 3 ||
	  dump_validate(buf, "####:##:##.# ") && sscanf(buf, "%x:%x:%x.%d", &mn, &bn, &dn, &fn) == 4 ||
	  dump_validate(buf, "#####:##:##.# ") && sscanf(buf, "%x:%x:%x.%d", &mn, &bn, &dn, &fn) == 4)
	{
	  dev = pci_get_dev(a, mn, bn, dn, fn);
	  dump_alloc_data(dev, 256);
	  pci_link_dev(a, dev);
	}
      else if (!len)
	dev = NULL;
      else if (dev &&
	       (dump_validate(buf, "##: ") || dump_validate(buf, "###: ") || dump_validate(buf, "####: ") ||
		dump_validate(buf, "#####: ") || dump_validate(buf, "######: ") ||
		dump_validate(buf, "#######: ") || dump_validate(buf, "########: ")) &&
	       sscanf(buf, "%x: ", &i) == 1)
	{
	  struct dump_data *dd = dev->backend_data;
	  z = strchr(buf, ' ') + 1;
	  while (isxdigit(z[0]) && isxdigit(z[1]) && (!z[2] || z[2] == ' ') &&
		 sscanf(z, "%x", &j) == 1 && j < 256)
	    {
	      if (i >= 4096)
		{
		  fclose(f);
		  a->error("dump: At most 4096 bytes of config space are supported");
		}
	      if (i >= dd->allocated)	/* Need to re-allocate the buffer */
		{
		  dump_alloc_data(dev, 4096);
		  memcpy(((struct dump_data *) dev->backend_data)->data, dd->data, 256);
		  pci_mfree(dd);
		  dd = dev->backend_data;
		}
	      dd->data[i++] = j;
	      if (i > dd->len)
		dd->len = i;
	      z += 2;
	      if (*z)
		z++;
	    }
	  if (*z)
	    {
	      fclose(f);
	      a->error("dump: Malformed line");
	    }
	}
    }
  fclose(f);
}

static void
dump_cleanup(struct pci_access *a UNUSED)
{
}

static void
dump_scan(struct pci_access *a UNUSED)
{
}

static int
dump_read(struct pci_dev *d, int pos, byte *buf, int len)
{
  struct dump_data *dd;
  if (!(dd = d->backend_data))
    {
      struct pci_dev *e = d->access->devices;
      while (e && (e->domain != d->domain || e->bus != d->bus || e->dev != d->dev || e->func != d->func))
	e = e->next;
      if (!e)
	return 0;
      dd = e->backend_data;
    }
  if (pos + len > dd->len)
    return 0;
  memcpy(buf, dd->data + pos, len);
  return 1;
}

static int
dump_write(struct pci_dev *d UNUSED, int pos UNUSED, byte *buf UNUSED, int len UNUSED)
{
  d->access->error("Writing to dump files is not supported.");
  return 0;
}

static void
dump_cleanup_dev(struct pci_dev *d)
{
  if (d->backend_data)
    {
      pci_mfree(d->backend_data);
      d->backend_data = NULL;
    }
}

struct pci_methods pm_dump = {
  .name = "dump",
  .help = "Reading of register dumps (set the `dump.name' parameter)",
  .config = dump_config,
  .detect = dump_detect,
  .init = dump_init,
  .cleanup = dump_cleanup,
  .scan = dump_scan,
  .fill_info = pci_generic_fill_info,
  .read = dump_read,
  .write = dump_write,
  .cleanup_dev = dump_cleanup_dev,
};
