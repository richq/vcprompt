/*
 * Copyright (C) 2009-2013, Gregory P. Ward and contributors.
 *
 * This is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sqlite3.h>
#include <unistd.h>
#include "common.h"
#include "svn.h"

#include <ctype.h>

static int
svn_probe(vccontext_t *context)
{
    return isdir(".svn");
}

static result_t*
svn_get_info(vccontext_t *context)
{
    result_t *result = init_result();
    FILE *fp = NULL;
    char buf[1024];

    int retval;
    sqlite3 *conn;
    sqlite3_stmt *res;
    const char *tail;

    if (!read_first_line(".svn/entries", buf, 1024)) {
        debug("failed to read from .svn/entries: not an svn working copy");
        goto err;
    }

    fp = fopen(".svn/entries", "r");
    if (!fp) {
        debug("failed to open .svn/entries: not an svn working copy");
        goto err;
    }
    char line[1024];

    if (access(".svn/wc.db", F_OK) == 0) {
        // Custom file format (working copy created by svn >= 1.7)

        retval = sqlite3_open(".svn/wc.db", &conn);
        if (retval) {
            debug("error opening database in .svn/wc.db");
            goto err;
        }
        retval = sqlite3_prepare_v2(conn, "select max(revision) from NODES", 1000, &res, &tail);
        if (retval) {
            debug("error running query");
            goto err_sqlite;
        }
        else {
            sqlite3_step(res);
            sprintf(buf, "%s", sqlite3_column_text(res, 0));
            result->revision = strdup(buf);
            sqlite3_finalize(res);
            sqlite3_close(conn);
        }
    }
    else {
        // Custom file format (working copy created by svn <= 1.7)
        // Check the version
        if (fgets(line, sizeof(line), fp)) {
            if(isdigit(line[0])) {
                // Custom file format (working copy created by svn >= 1.4)

                // Read and discard line 2 (name), 3 (entries kind)
                if (fgets(line, sizeof(line), fp) == NULL ||
                    fgets(line, sizeof(line), fp) == NULL) {
                    debug("early EOF reading .svn/entries");
                    goto err;
                }

                // Get the revision number
                if (fgets(line, sizeof(line), fp)) {
                    chop_newline(line);
                    result->revision = strdup(line);
                    debug("read a svn revision from .svn/entries: '%s'", line);
                }
                else {
                    debug("early EOF: expected revision number");
                    goto err;
                }
            }
            else {
                // XML file format (working copy created by svn < 1.4)
                char rev[100];
                char *marker = "revision=";
                char *p = NULL;
                while (fgets(line, sizeof(line), fp))
                    if ((p = strstr(line, marker)) != NULL)
                        break;
                if (p == NULL) {
                    debug("no 'revision=' line found in .svn/entries");
                    goto err;
                }
                if (sscanf(p, " %*[^\"]\"%[0-9]\"", rev) == 1) {
                    result_set_revision(result, rev, -1);
                    debug("read svn revision from .svn/entries: '%s'", rev);
                }
            }
        }
    }
    fclose(fp);
    return result;

 err_sqlite:
    sqlite3_finalize(res);
    sqlite3_close(conn);

 err:
    free(result);
    if (fp)
        fclose(fp);
    return NULL;
}

vccontext_t*
get_svn_context(options_t *options)
{
    return init_context("svn", options, svn_probe, svn_get_info);
}
