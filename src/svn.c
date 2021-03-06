/*
 * Copyright (C) 2009-2014, Gregory P. Ward and contributors.
 *
 * This is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "../config.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#if HAVE_SQLITE3
# include <sqlite3.h>
#endif

#include "common.h"
#include "svn.h"

#include <ctype.h>

static int
svn_probe(vccontext_t *context)
{
    return isdir(".svn");
}

static void
copy_path_section(char *dest, const char *src)
{
    while (*src != 0 && *src != '/')
        *dest++ = *src++;
}

static char *
simplify_branch(const char *branch)
{
    char *p = NULL;
    /* strip leading / from branch name */
    while (*branch == '/')
        branch++;
    /* convert common repo styles:
     *   branches/bar => bar
     *   branches/bar/baz/buzz => bar
     *   foo/branches/bar => foo/bar
     *   foo/branches/bar/baz/buzz => foo/bar
     * unchanged:
     *   branches
     *   foo
     */
    p = malloc(strlen(branch));
    char *tmp = p;
    const char branches[] = "branches/";
    const char trunk[] = "trunk/";

    if (strncmp(branch, branches, sizeof(branches) - 1) == 0) {
        branch += sizeof(branches) - 1;
        copy_path_section(tmp, branch);
    } else {
        if (strncmp(branch, trunk, sizeof(trunk) - 1) == 0) {
            copy_path_section(tmp, branch);
        } else {
            do {
                *tmp++ = *branch++;
            } while (*branch != 0 && *branch != '/');
            if (*branch != 0) {
                *tmp++ = *branch++;
                if (strncmp(branch, branches, sizeof(branches) - 1) == 0)
                    branch += sizeof(branches) - 1;
                copy_path_section(tmp, branch);
            } else {
                *tmp = 0;
            }
        }
    }
    return p;
}

static char *
get_branch_name(char *repos_path)
{
    // if repos_path endswith "trunk"
    //     return "trunk"
    // else if repos_path endswith "branches/*"
    //     return whatever matched "*"
    // else
    //     no idea

    // if the final component is "trunk", that's where we are
    char *slash = strrchr(repos_path, '/');
    char *name = slash ? slash + 1 : repos_path;
    if (strcmp(name, "trunk") == 0) {
        debug("found svn trunk");
        return strdup(name);
    }
    return simplify_branch(repos_path);
}


#if HAVE_SQLITE3
static int
svn_read_sqlite(vccontext_t *context, result_t *result)
{
    int ok = 0;
    int retval;
    sqlite3 *conn = NULL;
    sqlite3_stmt *res = NULL;
    const char *tail;
    char * repos_path = NULL;

    retval = sqlite3_open_v2(".svn/wc.db", &conn, SQLITE_OPEN_READONLY, NULL);
    if (retval != SQLITE_OK) {
        debug("error opening database in .svn/wc.db: %s", sqlite3_errmsg(conn));
        goto err;
    }
    // unclear when wc_id is anything other than 1
    char *sql = ("select changed_revision from nodes "
                 "where wc_id = 1 and local_relpath = ''");
    const char *textval;
    retval = sqlite3_prepare_v2(conn, sql, strlen(sql), &res, &tail);
    if (retval != SQLITE_OK) {
        debug("error running query: %s", sqlite3_errmsg(conn));
        goto err;
    }
    retval = sqlite3_step(res);
    if (retval != SQLITE_DONE && retval != SQLITE_ROW) {
        debug("error fetching result row: %s", sqlite3_errmsg(conn));
        goto err;
    }
    textval = (const char *) sqlite3_column_text(res, 0);
    if (textval == NULL) {
        debug("could not retrieve value of nodes.changed_revision");
        goto err;
    }
    result->revision = strdup(textval);
    sqlite3_finalize(res);

    sql = "select repos_path from nodes where local_relpath = ?";
    retval = sqlite3_prepare_v2(conn, sql, strlen(sql), &res, &tail);
    if (retval != SQLITE_OK) {
        debug("error querying for repos_path: %s", sqlite3_errmsg(conn));
        goto err;
    }
    retval = sqlite3_bind_text(res, 1,
                               context->rel_path, strlen(context->rel_path),
                               SQLITE_STATIC);
    if (retval != SQLITE_OK) {
        debug("error binding parameter: %s", sqlite3_errmsg(conn));
        goto err;
    }
    retval = sqlite3_step(res);
    if (retval != SQLITE_DONE && retval != SQLITE_ROW) {
        debug("error fetching result row: %s", sqlite3_errmsg(conn));
        goto err;
    }

    textval = (const char *) sqlite3_column_text(res, 0);
    if (textval == NULL) {
        debug("could not retrieve value of nodes.repos_path");
        goto err;
    }
    repos_path = strdup(textval);
    result->branch = get_branch_name(repos_path);

    ok = 1;

 err:
    if (res != NULL)
        sqlite3_finalize(res);
    if (conn != NULL)
        sqlite3_close(conn);
    if (repos_path != NULL)
        free(repos_path);
    return ok;
}
#else
static int
svn_read_sqlite(vccontext_t *context, result_t *result)
{
    debug("vcprompt built without sqlite3 (cannot support svn >= 1.7)");
    return 0;
}
#endif

static int
svn_read_custom(FILE *fp, char line[], int size, int line_num, result_t *result)
{
    // Caller has already read line 1. Read lines 2..5, discarding 2..4.
    while (line_num <= 5) {
        if (fgets(line, size, fp) == NULL) {
            debug(".svn/entries: early EOF (line %d empty)", line_num);
            return 0;
        }
        line_num++;
    }

    // Line 5 is the complete URL for the working dir (repos_root
    // + repos_path). To parse it easily, we first need the
    // repos_root from line 6.
    char *repos_root;
    int root_len;
    char *repos_path = strdup(line);
    chop_newline(repos_path);
    if (fgets(line, size, fp) == NULL) {
        debug(".svn/entries: early EOF (line %d empty)", line_num);
        return 0;
    }
    line_num++;
    repos_root = line;
    chop_newline(repos_root);
    root_len = strlen(repos_root);
    if (strncmp(repos_path, repos_root, root_len) != 0) {
        debug(".svn/entries: repos_path (%s) does not start with "
              "repos_root (%s)",
              repos_path, repos_root);
        free(repos_path);
        return 0;
    }
    result->branch = get_branch_name(repos_path + root_len + 1);
    free(repos_path);

    // Lines 6 .. 10 are also uninteresting.
    while (line_num <= 11) {
        if (fgets(line, size, fp) == NULL) {
            debug(".svn/entries: early EOF (line %d empty)", line_num);
            return 0;
        }
        line_num++;
    }

    // Line 11 is the revision number we care about, now in 'line'.
    chop_newline(line);
    result->revision = strdup(line);
    debug("read svn revision from .svn/entries: '%s'", line);
    return 1;
}

static int
svn_read_xml(FILE *fp, char line[], int size, int line_num, result_t *result)
{
    char rev[100];
    char *marker = "revision=";
    char *p = NULL;
    while (fgets(line, size, fp)) {
        if ((p = strstr(line, marker)) != NULL) {
            break;
        }
    }
    if (p == NULL) {
        debug("no 'revision=' line found in .svn/entries");
        return 0;
    }
    if (sscanf(p, " %*[^\"]\"%[0-9]\"", rev) == 1) {
        result_set_revision(result, rev, -1);
        debug("read svn revision from .svn/entries: '%s'", rev);
    }
    return 1;
}

static int
svn_should_ignore_modified(void)
{
    char initial_wd[BUFSIZ];
    char *result = getcwd(initial_wd, sizeof(initial_wd));
    int ignore_modified = 0;
    struct stat buf;
    if (result == NULL)
        return 0;
    while (1) {
        if (should_ignore_modified(".svn")) {
            ignore_modified = 1;
            break;
        }
        if (stat("../.svn", &buf) == 0 && S_ISDIR(buf.st_mode)) {
            if (chdir("..") != 0)
                break;
        } else {
            ignore_modified = should_ignore_modified(".svn");
            break;
        }
    }
    if (chdir(initial_wd) == -1)
        debug("error returning to %s", initial_wd);
    return ignore_modified;
}

static result_t*
svn_get_info(vccontext_t *context)
{
    result_t *result = init_result();
    FILE *fp = NULL;
    int ok = 0;

    if (access(".svn/wc.db", F_OK) == 0) {
        // SQLite file format (working copy created by svn >= 1.7)
        // Some repositories do not have the ".svn/entries" file anymore
        ok = svn_read_sqlite(context, result);
    }
    else {
        debug("cannot access() .svn/wc.db: not an svn >= 1.7 working copy");

        fp = fopen(".svn/entries", "r");
        if (!fp) {
            debug("failed to open .svn/entries: not an svn < 1.7 working copy");
            goto err;
        }
        char line[1024];
        int line_num = 1;                   // the line we're about to read

        if (fgets(line, sizeof(line), fp) == NULL) {
            debug(".svn/entries: empty file");
            goto err;
        }
        line_num++;

        // First line of the file tells us what the format is.
        if (isdigit(line[0])) {
            // Custom file format (working copy created by svn >= 1.4)
            ok = svn_read_custom(fp, line, sizeof(line), line_num, result);
        }
        else {
            // XML file format (working copy created by svn < 1.4)
            ok = svn_read_xml(fp, line, sizeof(line), line_num, result);
        }
    }
    if (context->options->show_modified) {
        int ignore_modified = svn_should_ignore_modified();
        if (!ignore_modified && is_cwd_remote())
            ignore_modified = 1;
        if (!ignore_modified) {
            debug("svn show modified");
            FILE *version = popen("svnversion -n", "r");
            if (version != NULL) {
                char buffer[256];
                char *gets_result = fgets(buffer, sizeof(buffer) - 1, version);
                if (gets_result != NULL) {
                    size_t len = strlen(buffer);
                    debug("svn version result %s", buffer);
                    result->modified = buffer[len - 1] == 'M';
                }
                pclose(version);
            }
        }
    }

 err:
    if (fp) {
        fclose(fp);
    }
    if (!ok) {
        free(result);
        result = NULL;
    }
    return result;
}

vccontext_t*
get_svn_context(options_t *options)
{
    return init_context("svn", options, svn_probe, svn_get_info);
}
