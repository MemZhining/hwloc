/*
 * Copyright © 2021-2023 Inria.  All rights reserved.
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <assert.h>

#include <level_zero/ze_api.h>
#include <level_zero/zes_api.h>

#include "private/autogen/config.h" /* for HWLOC_HAVE_ZESINIT */
#include "hwloc.h"
#include "hwloc/levelzero.h"

/* check the RSMI helpers */

static int check_levelzero_backend(hwloc_topology_t topology)
{
  struct hwloc_infos_s *infos = hwloc_topology_get_infos(topology);
  unsigned i;
  for(i=0; i<infos->count; i++)
    if (!strcmp(infos->array[i].name, "Backend")
        && !strcmp(infos->array[i].value, "LevelZero"))
      return 1;
  return 0;
}

int main(void)
{
  hwloc_topology_t topology;
  ze_driver_handle_t *drh;
  uint32_t nbdrivers, i, k;
  ze_result_t res;
  int has_levelzero_backend;
  int err = 0;

#ifdef HWLOC_HAVE_ZESINIT
  res = zesInit(0);
  if (res != ZE_RESULT_SUCCESS) {
    fprintf(stderr, "Failed to initialize LevelZero Sysman in zesInit(): %d\n", (int)res);
    /* continuing, assuming ZES_ENABLE_SYSMAN=1 will be enough */
  }
#endif

  putenv((char *) "ZES_ENABLE_SYSMAN=1");

  res = zeInit(0);
  if (res != ZE_RESULT_SUCCESS) {
    fprintf(stderr, "Failed to initialize LevelZero in zeInit(): %d\n", (int)res);
    return 0;
  }
  
  hwloc_topology_init(&topology);
  hwloc_topology_set_io_types_filter(topology, HWLOC_TYPE_FILTER_KEEP_IMPORTANT);
  hwloc_topology_load(topology);

  has_levelzero_backend = check_levelzero_backend(topology);

  nbdrivers = 0;
  res = zeDriverGet(&nbdrivers, NULL);
  if (res != ZE_RESULT_SUCCESS || !nbdrivers)
    return 0;
  drh = malloc(nbdrivers * sizeof(*drh));
  if (!drh)
    return 0;
  res = zeDriverGet(&nbdrivers, drh);
  if (res != ZE_RESULT_SUCCESS) {
    free(drh);
    return 0;
  }

  printf("found %u L0 drivers\n", nbdrivers);

  k = 0;
  for(i=0; i<nbdrivers; i++) {
    uint32_t nbdevices, j;
    ze_device_handle_t *dvh;

    nbdevices = 0;
    res = zeDeviceGet(drh[i], &nbdevices, NULL);
    if (res != ZE_RESULT_SUCCESS || !nbdevices)
      continue;
    dvh = malloc(nbdevices * sizeof(*dvh));
    if (!dvh)
      continue;
    res = zeDeviceGet(drh[i], &nbdevices, dvh);
    if (res != ZE_RESULT_SUCCESS) {
      free(dvh);
      continue;
    }

    printf("found %u L0 devices in driver #%u\n", nbdevices, i);

    for (j=0; j<nbdevices; j++, k++) {
      hwloc_bitmap_t set;
      hwloc_obj_t osdev, ancestor;
      const char *value;

      osdev = hwloc_levelzero_get_device_osdev(topology, dvh[j]);
      assert(osdev);

      ancestor = hwloc_get_non_io_ancestor_obj(topology, osdev);

      printf("found OSDev %s\n", osdev->name);
      err = strncmp(osdev->name, "ze", 2);
      assert(!err);
      assert(atoi(osdev->name+2) == (int) k);

      assert(osdev->attr->osdev.type == HWLOC_OBJ_OSDEV_COPROC);

      assert(has_levelzero_backend);

      value = hwloc_obj_get_info_by_name(osdev, "LevelZeroDriverIndex");
      assert(value);
      assert(atoi(value) == (int) i);
      value = hwloc_obj_get_info_by_name(osdev, "LevelZeroDriverDeviceIndex");
      assert(value);
      assert(atoi(value) == (int) j);

      set = hwloc_bitmap_alloc();
      err = hwloc_levelzero_get_device_cpuset(topology, dvh[j], set);
      if (err < 0) {
        printf("failed to get cpuset for driver #%u device #%u\n", i, j);
      } else {
        char *cpuset_string = NULL;
        hwloc_bitmap_asprintf(&cpuset_string, set);
        printf("got cpuset %s for driver #%u device #%u\n", cpuset_string, j, i);
        free(cpuset_string);
        if (hwloc_bitmap_isequal(hwloc_topology_get_complete_cpuset(topology), hwloc_topology_get_topology_cpuset(topology)))
          /* only compare if the topology is complete, otherwise things can be significantly different */
          assert(hwloc_bitmap_isequal(set, ancestor->cpuset));
      }
      hwloc_bitmap_free(set);
    }
  }

  hwloc_topology_destroy(topology);

  return err;
}
