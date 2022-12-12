#define _GNU_SOURCE

#include <libudev.h>
#include <stdarg.h>
#include <stdio.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <fnmatch.h>

#include "inih/ini.c"

#define LIBNAME "libdenydevice"
#define ENV_DEBUG "LIBDD_DEBUG"
#define ENV_CONFIGFILE "LIBDD_CONFIG"
#define INI_SECTION_PATTERNS "patterns"
#define INI_SECTION_ATTRIBUTES "attributes"

#define LIST_ENTRY_FIELDS(t) t* next

typedef struct list_entry
{
  LIST_ENTRY_FIELDS(struct list_entry);
} list_entry;

typedef struct pattern
{
  LIST_ENTRY_FIELDS(struct pattern);

  char* pattern;
} pattern;

typedef struct device_attribute
{
  LIST_ENTRY_FIELDS(struct device_attribute);

  char* name;
  char* value;
} device_attribute;

static bool initialized = false;
static bool write_log = false;
static bool active = false;
static char* config_file = NULL;
static pattern* patterns = NULL;
static device_attribute* device_attributes = NULL;

static int (*old_open) (const char* pathname, int flags, mode_t mode);
static FILE* (*old_fopen) (const char* filename, const char* mode);
static FILE* (*old_fopen64) (const char* filename, const char* mode);
static struct udev_list_entry* (*old_udev_list_entry_get_next) (struct udev_list_entry* list_entry);

void log_debug(const char* format, ...)
{
  if (!write_log)
    return;

  fprintf(stderr, "%s/%d: ", LIBNAME, getpid());

  va_list args;
  va_start(args, format);

  vfprintf(stderr, format, args);

  va_end(args);

  fprintf(stderr, "\n");
}

void add_list_entry(list_entry** list, list_entry* new_entry)
{
  new_entry->next = NULL;

  if (!*list)
  {
    *list = new_entry;
    return;
  }

  list_entry* list_entry = *list;

  while (list_entry->next)
    list_entry = list_entry->next;

  list_entry->next = new_entry;
}

static int ini_parse_handler(void* user, const char* section, const char* name, const char* value)
{
  if (strcmp(section, INI_SECTION_PATTERNS) == 0)
  {
    pattern* pattern = malloc(sizeof(pattern));
    pattern->pattern = strdup(value);

    add_list_entry((list_entry**)&patterns, (list_entry*)pattern);
  }
  else if (strcmp(section, INI_SECTION_ATTRIBUTES) == 0)
  {
    device_attribute* attribute = malloc(sizeof(device_attribute));
    attribute->name = strdup(name);
    attribute->value = strdup(value);

    add_list_entry((list_entry**)&device_attributes, (list_entry*)attribute);
  }
  else
    return 0;

  return 1;
}

bool initialize(void)
{
  if (initialized)
    return active;

  initialized = true;

  write_log = getenv(ENV_DEBUG) != NULL && strcmp(getenv(ENV_DEBUG), "1") == 0;
  config_file = getenv(ENV_CONFIGFILE);

  if (config_file == NULL || *config_file == '\0')
  {
    log_debug("Environment variable '%s' not set", ENV_CONFIGFILE);
    return false;
  }

  if (ini_parse(config_file, ini_parse_handler, NULL) < 0)
  {
    log_debug("Error loading config file '%s'", config_file);
    return false;
  }

  if (patterns == NULL || device_attributes == NULL)
  {
    log_debug("No patterns and/or attributes configured");
    return false;
  }

  log_debug("Using file patterns:");
  for (pattern* pattern = patterns; pattern; pattern = pattern->next)
    log_debug("  %s", pattern->pattern);

  log_debug("Denying access for udev attributes/values:");
  for (device_attribute* attribute = device_attributes; attribute; attribute = attribute->next)
    log_debug("  %s=%s", attribute->name, attribute->value);

  return active = true;
}

bool udev_device_allowed(struct udev_device* dev)
{
  struct udev_list_entry* attributes = udev_device_get_sysattr_list_entry(dev);
  while (attributes != NULL)
  {
    for (device_attribute* attribute = device_attributes; attribute; attribute = attribute->next)
      if (strcasecmp(udev_list_entry_get_name(attributes), attribute->name) == 0 && strcmp(udev_device_get_sysattr_value(dev, udev_list_entry_get_name(attributes)), attribute->value) == 0)
        return false;

    attributes = old_udev_list_entry_get_next(attributes);
  }

  struct udev_device* parent_dev = udev_device_get_parent(dev);

  return parent_dev != NULL ? udev_device_allowed(parent_dev) : true;
}

bool device_allowed(const char* devicepath)
{
  bool not_matched = true;
  for (pattern* pattern = patterns; pattern; pattern = pattern->next)
    if (fnmatch(pattern->pattern, devicepath, 0) == 0)
    {
      not_matched = false;
      break;
    }

  if (not_matched)
    return true;

  if (old_udev_list_entry_get_next == NULL)
    old_udev_list_entry_get_next = dlsym(RTLD_NEXT, "udev_list_entry_get_next");

  log_debug("Checking device '%s'", devicepath);

  struct udev* udev = udev_new();
  if (!udev)
  {
    log_debug("udev_new() failed");

    return true;
  }

  struct udev_enumerate* enumerate = udev_enumerate_new(udev);
  if (!enumerate)
  {
    log_debug("udev_enumerate_new() failed");

    udev_unref(udev);

    return true;
  }

  udev_enumerate_add_match_subsystem(enumerate, "input");
  udev_enumerate_add_match_subsystem(enumerate, "hidraw");
  udev_enumerate_scan_devices(enumerate);

  struct udev_list_entry* devices = udev_enumerate_get_list_entry(enumerate);
  if (!devices)
  {
    log_debug("udev_enumerate_get_list_entry() failed");

    udev_enumerate_unref(enumerate);
    udev_unref(udev);

    return true;
  }

  for (struct udev_list_entry* dev_list_entry = devices; dev_list_entry != NULL; dev_list_entry = old_udev_list_entry_get_next(dev_list_entry))
  {
    const char* path = udev_list_entry_get_name(dev_list_entry);
    struct udev_device* dev = udev_device_new_from_syspath(udev, path);

    const char* device_node = udev_device_get_devnode(dev);

    if (device_node != NULL && strcmp(device_node, devicepath) == 0 && !udev_device_allowed(dev))
    {
      udev_device_unref(dev);
      udev_enumerate_unref(enumerate);
      udev_unref(udev);

      return false;
    }

    udev_device_unref(dev);
  }

  udev_enumerate_unref(enumerate);
  udev_unref(udev);

  return true;
}

FILE* fopen(const char* filename, const char* mode)
{
  // log_debug("Intercepted fopen(%s)", filename);

  if (old_fopen == NULL)
    old_fopen = dlsym(RTLD_NEXT, __func__);

  if (!initialize() || device_allowed(filename))
    return old_fopen(filename, mode);

  log_debug("Denying access to '%s'", filename);

  errno = EPERM;
  return NULL;
}

FILE* fopen64(const char* filename, const char* mode)
{
  // log_debug("Intercepted fopen64(%s)", filename);

  if (old_fopen64 == NULL)
    old_fopen64 = dlsym(RTLD_NEXT, __func__);

  if (!initialize() || device_allowed(filename))
    return old_fopen64(filename, mode);

  log_debug("Denying access to '%s'", filename);

  errno = EPERM;
  return NULL;
}

int open(const char* pathname, int flags, mode_t mode)
{
  // log_debug("Intercepted open(%s)", pathname);

  if (old_open == NULL)
    old_open = dlsym(RTLD_NEXT, __func__);

  if (!initialize() || device_allowed(pathname))
    return old_open(pathname, flags, mode);

  log_debug("Denying access to '%s'", pathname);

  errno = EPERM;
  return -1;
}

struct udev_list_entry* udev_list_entry_get_next(struct udev_list_entry* list_entry)
{
  if (old_udev_list_entry_get_next == NULL)
    old_udev_list_entry_get_next = dlsym(RTLD_NEXT, __func__);

  if (!initialize())
    return old_udev_list_entry_get_next(list_entry);

  struct udev_list_entry* res = old_udev_list_entry_get_next(list_entry);

  if (res != NULL)
  {
    const char* path = udev_list_entry_get_name(res);

    struct udev* udev = udev_new();
    struct udev_device* dev = udev_device_new_from_syspath(udev, path);

    if (!udev_device_allowed(dev))
    {
      log_debug("Hiding udev device '%s'", udev_device_get_devnode(dev) != NULL ? udev_device_get_devnode(dev) : udev_device_get_syspath(dev));

      udev_device_unref(dev);
      udev_unref(udev);

      return udev_list_entry_get_next(res);
    }

    udev_device_unref(dev);
    udev_unref(udev);
  }

  return res;
}
