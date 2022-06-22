// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2021 IBM Corp.
#include "ahb.h"
#include "ast.h"
#include "clk.h"
#include "compiler.h"
#include "host.h"
#include "log.h"
#include "priv.h"
#include "soc.h"
#include "wdt.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int cmd_reset(const char *name __unused, int argc, char *argv[])
{
    struct host _host, *host = &_host;
    struct soc _soc, *soc = &_soc;
    struct clk _clk, *clk = &_clk;
    struct wdt _wdt, *wdt = &_wdt;
    int64_t wait = 0;
    struct ahb *ahb;
    int cleanup;
    int rc;

    /* reset subcommand argument validation and parsing */
    if (argc < 2) {
        loge("Not enough arguments for reset command\n");
        exit(EXIT_FAILURE);
    }

    if (strcmp("soc", argv[0])) {
        loge("Unsupported reset type: '%s'\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    if ((rc = host_init(host, argc - 2, argv + 2)) < 0) {
        loge("Failed to acquire AHB interface, exiting: %d\n", rc);
        exit(EXIT_FAILURE);
    }

    if (!(ahb = host_get_ahb(host))) {
        loge("Failed to acquire AHB interface, exiting\n");
        exit(EXIT_FAILURE);
    }

    if ((rc = soc_probe(soc, ahb)) < 0) {
        errno = -rc;
        perror("soc_probe");
        goto cleanup_host;
    }

    /* Probe the SoC */
    if ((rc = soc_probe(soc, ahb)) < 0) {
        errno = -rc;
        perror("soc_probe");
        goto cleanup_host;
    }

    /* Initialise the required SoC drivers */
    if ((rc = clk_init(clk, soc))) {
        errno = -rc;
        perror("clk_init");
        goto cleanup_soc;
    }

    if ((rc = wdt_init(wdt, soc, argv[1])) < 0) {
        errno = -rc;
        perror("wdt_init");
        goto cleanup_clk;
    }

    /* Do the reset */
    if (ahb->bridge != ahb_devmem) {
        logi("Gating ARM clock\n");
        rc = clk_disable(clk, clk_arm);
        if (rc < 0)
            goto cleanup_wdt;
    }

    logi("Preventing system reset\n");
    if ((rc = wdt_prevent_reset(soc)) < 0) {
        errno = -rc;
        perror("wdt_prevent_reset");
        goto clk_enable_arm;
    }

    /* wdt_perform_reset ungates the ARM if required */
    logi("Performing SoC reset\n");
    if ((wait = wdt_perform_reset(wdt)) > 0)
        usleep(wait);

    /* Cleanup */
    if (wait < 0) {
clk_enable_arm:
        if ((cleanup = clk_enable(clk, clk_arm)) < 0) {
            errno = -cleanup;
            perror("clk_enable");
        }
    }

cleanup_wdt:
    wdt_destroy(wdt);

cleanup_clk:
    clk_destroy(clk);

cleanup_soc:
    soc_destroy(soc);

cleanup_host:
    host_destroy(host);

    return rc;
}
