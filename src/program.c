/*
 * PLUTO: An automatic parallelizer and locality optimizer
 * 
 * Copyright (C) 2007-2012 Uday Bondhugula
 *
 * This file is part of Pluto.
 *
 * Pluto is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * A copy of the GNU General Public Licence can be found in the file
 * `LICENSE' in the top-level directory of this distribution. 
 *
 * program.c
 *
 * This file contains functions that do the job interfacing the PLUTO 
 * core to the frontend and related matters
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <assert.h>
#include <math.h>

#include "pluto.h"
#include "math_support.h"
#include "program.h"

#include "scoplib/statement.h"
#include "scoplib/access.h"

#include <isl/map.h>
#include <isl/mat.h>
#include <isl/set.h>
#include <isl/flow.h>
#include <isl/union_map.h>

void pluto_add_dep(PlutoProg *prog, Dep *dep)
{
    dep->id = prog->ndeps;
    prog->ndeps++;
    prog->deps = (Dep **) realloc(prog->deps, sizeof(Dep *)*prog->ndeps);
    prog->deps[prog->ndeps-1] = dep;
}

/*
 * Computes the transitive dependence via dep1 and dep2 
 * Note: dep1's target statement should be same as dep2's source
 */
Dep *pluto_dep_compose(Dep *dep1, Dep *dep2, PlutoProg *prog)
{
    int i;

    assert(dep1->dest == dep2->src);

    Stmt *s1 = prog->stmts[dep1->src];
    Stmt *s2 = prog->stmts[dep2->src];
    Stmt *s3 = prog->stmts[dep2->dest];

    PlutoConstraints *d1 = pluto_constraints_dup(dep1->dpolytope);
    PlutoConstraints *d2 = pluto_constraints_dup(dep2->dpolytope);

    for (i=0; i<s3->dim ; i++) {
        pluto_constraints_add_dim(d1, s1->dim+s2->dim);
    }
    for (i=0; i<s1->dim; i++) {
        pluto_constraints_add_dim(d2, 0);
    }

    PlutoConstraints *d3poly = pluto_constraints_dup(d1);
    pluto_constraints_add(d3poly, d2);

    pluto_constraints_project_out(d3poly, s1->dim, s2->dim);
    pluto_constraints_free(d1);
    pluto_constraints_free(d2);

    if (pluto_constraints_is_empty(d3poly)) {
        pluto_constraints_free(d3poly);
        return NULL;
    }

    Dep *dep = pluto_dep_alloc();

    dep->src = dep1->src;
    dep->dest = dep2->dest;
    dep->src_acc = dep1->src_acc;
    dep->dest_acc = dep2->dest_acc;

    dep->dpolytope = d3poly;

    return dep;
}

PlutoMatrix *scoplib_schedule_to_pluto_trans(scoplib_matrix_p smat)
{
    int i, j;

    PlutoMatrix *mat;

    mat = pluto_matrix_alloc(smat->NbRows, smat->NbColumns-1);
    for (i=0; i<smat->NbRows; i++)  {
        /* Only equalities in schedule expected */
        assert(smat->p[i][0] == 0);

        for (j=1; j<smat->NbColumns; j++)  {
            mat->val[i][j-1] = smat->p[i][j];
        }
    }

    return mat;
}

scoplib_matrix_p pluto_trans_to_scoplib_schedule(PlutoMatrix *mat)
{
    int i, j;

    scoplib_matrix_p smat;
    smat = scoplib_matrix_malloc(mat->nrows, mat->ncols+1);

    for (i=0; i<mat->nrows; i++)  {
        /* Only equalities in schedule expected */
        smat->p[i][0] = 0;

        for (j=0; j<mat->ncols; j++)  {
            smat->p[i][j+1] = mat->val[i][j];
        }
    }

    return smat;
}

PlutoMatrix *scoplib_matrix_to_pluto_matrix(scoplib_matrix_p smat)
{
    int i, j;

    PlutoMatrix *mat;

    mat = pluto_matrix_alloc(smat->NbRows, smat->NbColumns);
    for (i=0; i<smat->NbRows; i++)  {
        for (j=0; j<smat->NbColumns; j++)  {
            mat->val[i][j] = smat->p[i][j];
        }
    }

    return mat;
}


scoplib_matrix_p pluto_matrix_to_scoplib_matrix(PlutoMatrix *mat)
{
    int i, j;

    scoplib_matrix_p smat;

    smat = scoplib_matrix_malloc(mat->nrows, mat->ncols);
    for(i=0; i<mat->nrows; i++){
        for(j=0; j<mat->ncols; j++){
            smat->p[i][j] = mat->val[i][j];
        }
    }

    return smat;
}

PlutoConstraints *scoplib_matrix_to_pluto_constraints(scoplib_matrix_p clanMatrix)
{
    int i, j;
    PlutoConstraints *cst;

    cst = pluto_constraints_alloc(clanMatrix->NbRows, clanMatrix->NbColumns-1);
    cst->nrows = clanMatrix->NbRows;

    for (i=0; i<clanMatrix->NbRows; i++)   {
        cst->is_eq[i] = (clanMatrix->p[i][0] == 0);
        for (j=0; j<cst->ncols; j++)   {
            cst->val[i][j] = (int) clanMatrix->p[i][j+1];
        }
    }
    return cst;
}

scoplib_matrix_p pluto_constraints_to_scoplib_matrix(PlutoConstraints *cst)
{
    int i, j;
    scoplib_matrix_p smat;

    smat = scoplib_matrix_malloc(cst->nrows, cst->ncols+1);

    for(i=0; i<cst->nrows; i++){
        smat->p[i][0] = (cst->is_eq[i] == 0);
        for(j=0; j<cst->ncols; j++){
            smat->p[i][j+1] = cst->val[i][j];
        }
    }
    return smat;
}

scoplib_matrix_list_p pluto_constraints_list_to_scoplib_matrix_list(PlutoConstraints *cst){

    scoplib_matrix_list_p list = scoplib_matrix_list_malloc();

    assert (cst != NULL);

    list->elt = pluto_constraints_to_scoplib_matrix(cst);

    if(cst->next != NULL)
        list->next = pluto_constraints_list_to_scoplib_matrix_list(cst->next);

    return list;
}

PlutoConstraints *candl_matrix_to_pluto_constraints(const CandlMatrix *candlMatrix)
{
    int i, j;
    PlutoConstraints *cst;

    cst = pluto_constraints_alloc(candlMatrix->NbRows, candlMatrix->NbColumns-1);
    cst->nrows = candlMatrix->NbRows;
    cst->ncols = candlMatrix->NbColumns-1;

    for (i=0; i<candlMatrix->NbRows; i++)   {
        if (candlMatrix->p[i][0] == 0) {
            cst->is_eq[i] = 1;
        }else{
            cst->is_eq[i] = 0;
        }

        for (j=0; j<cst->ncols; j++)   {
            cst->val[i][j] = (int) candlMatrix->p[i][j+1];
        }
    }

    // pluto_matrix_print(stdout, cst);

    return cst;
}



/* Get the position of this access given a CandlStmt access matrix
 * (concatenated)
 * ref: starting row for a particular access in concatenated rows of
 * access functions
 * Return the position of this access in the list  */
static int get_access_position(CandlMatrix *accesses, int ref)
{
    int num, i;

    num = -1;
    for (i=0; i<=ref; i++)  {
        if (accesses->p[i][0] != 0)   {
            num++;
        }
    }
    assert(num >= 0);
    return num;
}


/* Read dependences from candl structures */
static Dep **deps_read(CandlDependence *candlDeps, PlutoProg *prog)
{
    int i, ndeps;
    Dep **deps;
    int npar = prog->npar;
    Stmt **stmts = prog->stmts;

    ndeps = candl_num_dependences(candlDeps);

    deps = (Dep **) malloc(ndeps*sizeof(Dep *));

    for (i=0; i<ndeps; i++) {
        deps[i] = pluto_dep_alloc();
    }

    CandlDependence *candl_dep = candlDeps;

    candl_dep = candlDeps;

    IF_DEBUG(candl_dependence_pprint(stdout, candl_dep));

    /* Dependence polyhedra information */
    for (i=0; i<ndeps; i++)  {
        Dep *dep = deps[i];
        dep->id = i;
        dep->type = candl_dep->type;
        dep->src = candl_dep->source->label;
        dep->dest = candl_dep->target->label;

        //candl_matrix_print(stdout, candl_dep->domain);
        dep->dpolytope = candl_matrix_to_pluto_constraints(candl_dep->domain);

        switch (dep->type) {
            case CANDL_RAW: 
                dep->src_acc = stmts[dep->src]->writes[
                    get_access_position(candl_dep->source->written, candl_dep->ref_source)];
                dep->dest_acc = stmts[dep->dest]->reads[
                    get_access_position(candl_dep->target->read, candl_dep->ref_target)];
                break;
            case CANDL_WAW: 
                dep->src_acc = stmts[dep->src]->writes[
                    get_access_position(candl_dep->source->written, candl_dep->ref_source)];
                dep->dest_acc = stmts[dep->dest]->writes[
                    get_access_position(candl_dep->target->written, candl_dep->ref_target)];
                break;
            case CANDL_WAR: 
                dep->src_acc = stmts[dep->src]->reads[
                    get_access_position(candl_dep->source->read, candl_dep->ref_source)];
                dep->dest_acc = stmts[dep->dest]->writes[
                    get_access_position(candl_dep->target->written, candl_dep->ref_target)];
                break;
            case CANDL_RAR: 
                dep->src_acc = stmts[dep->src]->reads[
                    get_access_position(candl_dep->source->read, candl_dep->ref_source)];
                dep->dest_acc = stmts[dep->dest]->reads[
                    get_access_position(candl_dep->target->read, candl_dep->ref_target)];
                break;
            default:
                assert(0);
        }

        /* Get rid of rows that are all zero */
        int r, c;
        bool *remove = (bool *) malloc(sizeof(bool)*dep->dpolytope->nrows);
        for (r=0; r<dep->dpolytope->nrows; r++) {
            for (c=0; c<dep->dpolytope->ncols; c++) {
                if (dep->dpolytope->val[r][c] != 0) {
                    break;
                }
            }
            if (c == dep->dpolytope->ncols) {
                remove[r] = true;
            }else{
                remove[r] = false;
            }
        }
        int orig_nrows = dep->dpolytope->nrows;
        int del_count = 0;
        for (r=0; r<orig_nrows; r++) {
            if (remove[r])  {
                pluto_constraints_remove_row(dep->dpolytope, r-del_count);
                del_count++;
            }
        }
        free(remove);

        int src_dim = stmts[dep->src]->dim;
        int target_dim = stmts[dep->dest]->dim;

        assert(candl_dep->domain->NbColumns-1 == src_dim+target_dim+npar+1);

        candl_dep = candl_dep->next;
    }

    return deps;
}

void pluto_dep_print(FILE *fp, Dep *dep)
{
    fprintf(fp, "--- Dep %d from S%d to S%d; satisfied: %d, sat level: %d; Type: ",
            dep->id+1, dep->src+1, dep->dest+1, dep->satisfied, dep->satisfaction_level);

    switch (dep->type) {
        case CANDL_UNSET : fprintf(fp, "UNSET"); break;
        case CANDL_RAW   : fprintf(fp, "RAW")  ; break;
        case CANDL_WAR   : fprintf(fp, "WAR")  ; break;
        case CANDL_WAW   : fprintf(fp, "WAW")  ; break;
        case CANDL_RAR   : fprintf(fp, "RAR")  ; break;
        default : fprintf(fp, "unknown"); break;
    }

    fprintf(fp, "\n");
    if (dep->src_acc != NULL) {
        fprintf(fp, "Var: %s\n", dep->src_acc->name);
    }

    fprintf(fp, "Dependence polyhedron\n");
    pluto_constraints_print(fp, dep->dpolytope);
    fprintf(fp, "\n");
}


void pluto_deps_print(FILE *fp, PlutoProg *prog)
{
    int i;
    for (i=0; i<prog->ndeps; i++) {
        pluto_dep_print(fp, prog->deps[i]);
    }
}


/* Read statement info from scoplib structures (nvar: max domain dim) */
static Stmt **scoplib_to_pluto_stmts(const scoplib_scop_p scop)
{
    int i, j;
    Stmt **stmts;
    int npar, nvar, nstmts, max_sched_rows;
    scoplib_statement_p scop_stmt;

    npar = scop->nb_parameters;
    nstmts = scoplib_statement_number(scop->statement);

    if (nstmts == 0)    return NULL;

    /* Max dom dimensionality */
    nvar = -1;
    max_sched_rows = 0;
    scop_stmt = scop->statement;
    for (i=0; i<nstmts; i++) {
        nvar = PLMAX(nvar, scop_stmt->nb_iterators);
        max_sched_rows = PLMAX(max_sched_rows, scop_stmt->schedule->NbRows);
        scop_stmt = scop_stmt->next;
    }

    /* Allocate more to account for unroll/jamming later on */
    stmts = (Stmt **) malloc(nstmts*sizeof(Stmt *));

    scop_stmt = scop->statement;

    for(i=0; i<nstmts; i++)  {
        PlutoConstraints *domain = 
            scoplib_matrix_to_pluto_constraints(scop_stmt->domain->elt);
        PlutoMatrix *trans = scoplib_schedule_to_pluto_trans(scop_stmt->schedule);

        // scoplib_matrix_print(stdout, scop_stmt->schedule);
        stmts[i] = pluto_stmt_alloc(scop_stmt->nb_iterators, domain, trans);

        /* Pad with all zero rows */
        int curr_sched_rows = stmts[i]->trans->nrows;
        for (j=curr_sched_rows; j<max_sched_rows; j++) {
            pluto_stmt_add_hyperplane(stmts[i], H_SCALAR, j);
        }

        pluto_constraints_free(domain);
        pluto_matrix_free(trans);

        Stmt *stmt = stmts[i];

        stmt->id = i;
        stmt->type = ORIG;

        assert(scop_stmt->domain->elt->NbColumns-1 == stmt->dim + npar + 1);

        for (j=0; j<stmt->dim; j++)  {
            stmt->is_orig_loop[j] = true;
        }

        /* Tile it if it's tilable unless turned off by .fst/.precut file */
        stmt->tile = 1;

        for (j=0; j<stmt->dim; j++)    {
            stmt->iterators[j] = strdup(scop_stmt->iterators[j]);
        }
        /* Statement text */
        stmt->text = (char *) malloc(sizeof(char)*(strlen(scop_stmt->body)+1));
        strcpy(stmt->text, scop_stmt->body);

        /* Read/write accesses */
        scoplib_access_list_p wlist = scoplib_access_get_write_access_list(scop, scop_stmt);
        scoplib_access_list_p rlist = scoplib_access_get_read_access_list(scop, scop_stmt);
        scoplib_access_list_p rlist_t, wlist_t;
        rlist_t = rlist;
        wlist_t = wlist;

        int count=0;
        scoplib_access_list_p tmp = wlist;
        while (tmp != NULL)   {
            count++;
            tmp = tmp->next;
        }
        stmt->nwrites = count;
        stmt->writes = (PlutoAccess **) malloc(stmt->nwrites*sizeof(PlutoAccess *));

        tmp = rlist;
        count = 0;
        while (tmp != NULL)   {
            count++;
            tmp = tmp->next;
        }
        stmt->nreads = count;
        stmt->reads = (PlutoAccess **) malloc(stmt->nreads*sizeof(PlutoAccess *));

        count = 0;
        while (wlist != NULL)   {
            PlutoMatrix *wmat = scoplib_matrix_to_pluto_matrix(wlist->elt->matrix);
            stmt->writes[count] = (PlutoAccess *) malloc(sizeof(PlutoAccess));
            stmt->writes[count]->mat = wmat;
            if (wlist->elt->symbol != NULL) {
                stmt->writes[count]->name = strdup(wlist->elt->symbol->identifier);
                stmt->writes[count]->symbol = scoplib_symbol_copy(wlist->elt->symbol);
            }else{
                stmt->writes[count]->name = NULL;
                stmt->writes[count]->symbol = NULL;
            }
            count++;
            wlist = wlist->next;
        }

        count = 0;
        while (rlist != NULL)   {
            PlutoMatrix *rmat = scoplib_matrix_to_pluto_matrix(rlist->elt->matrix);
            stmt->reads[count] = (PlutoAccess *) malloc(sizeof(PlutoAccess));
            stmt->reads[count]->mat = rmat;
            if (rlist->elt->symbol != NULL) {
                stmt->reads[count]->name = strdup(rlist->elt->symbol->identifier);
                stmt->reads[count]->symbol = scoplib_symbol_copy(rlist->elt->symbol);
            }
            else{
                stmt->reads[count]->name = NULL;
                stmt->reads[count]->symbol = NULL;
            }
            //scoplib_symbol_print(stdout, stmt->reads[count]->symbol);
            count++;
            rlist = rlist->next;
        }

        scoplib_access_list_free(wlist_t);
        scoplib_access_list_free(rlist_t);

        scop_stmt = scop_stmt->next;
    }

    return stmts;
}

void pluto_stmt_print(FILE *fp, const Stmt *stmt)
{
    int i;

    fprintf(fp, "S%d \"%s\"; ndims: %d; orig_depth: %d\n", 
            stmt->id+1, stmt->text, stmt->dim, stmt->dim_orig);
    fprintf(fp, "Domain\n");
    pluto_constraints_print(fp, stmt->domain);
    fprintf(fp, "Transformation\n");
    pluto_matrix_print(fp, stmt->trans);

    if (stmt->nreads==0) {
        fprintf(fp, "No Read accesses\n");
    }else{
        fprintf(fp, "Read accesses\n");
        for (i=0; i<stmt->nreads; i++)  {
            pluto_matrix_print(fp, stmt->reads[i]->mat);
        }
    }

    if (stmt->nwrites==0) {
        fprintf(fp, "No write access\n");
    }else{
        fprintf(fp, "Write accesses\n");
        for (i=0; i<stmt->nwrites; i++)  {
            pluto_matrix_print(fp, stmt->writes[i]->mat);
        }
    }

    for (i=0; i<stmt->dim; i++) {
        printf("Original loop: %d -> %d\n", i, stmt->is_orig_loop[i]);
    }

    fprintf(fp, "\n");
}


void pluto_stmts_print(FILE *fp, Stmt **stmts, int nstmts)
{
    int i;

    for(i=0; i<nstmts; i++)  {
        pluto_stmt_print(fp, stmts[i]);
    }
}


void pluto_prog_print(PlutoProg *prog)
{
    printf("nvar = %d, npar = %d\n", prog->nvar, prog->npar);

    pluto_stmts_print(stdout, prog->stmts, prog->nstmts);
    pluto_deps_print(stdout, prog);
    pluto_transformations_pretty_print(prog);
}


void pluto_dep_free(Dep *dep)
{
    pluto_constraints_free(dep->dpolytope);
    pluto_constraints_free(dep->depsat_poly);
    if (dep->dirvec) {
        free(dep->dirvec);
    }
    if (dep->dirvec) {
        free(dep->satvec);
    }
    free(dep);
}


/* Set the dimension names of type "type" according to the elements
 * in the array "names".
 */
static __isl_give isl_dim *set_names(__isl_take isl_dim *dim,
        enum isl_dim_type type, char **names)
{
    int i;

    for (i = 0; i < isl_dim_size(dim, type); ++i)
        dim = isl_dim_set_name(dim, type, i, names[i]);

    return dim;
}


/* Convert a scoplib_matrix_p containing the constraints of a domain
 * to an isl_set.
 */
static __isl_give isl_set *scoplib_matrix_to_isl_set(scoplib_matrix_p matrix,
        __isl_take isl_dim *dim)
{
    int i, j;
    int n_eq = 0, n_ineq = 0;
    isl_ctx *ctx;
    isl_mat *eq, *ineq;
    isl_int v;
    isl_basic_set *bset;

    isl_int_init(v);

    ctx = isl_dim_get_ctx(dim);

    for (i = 0; i < matrix->NbRows; ++i)
        if (SCOPVAL_zero_p(matrix->p[i][0]))
            n_eq++;
        else
            n_ineq++;

    eq = isl_mat_alloc(ctx, n_eq, matrix->NbColumns - 1);
    ineq = isl_mat_alloc(ctx, n_ineq, matrix->NbColumns - 1);

    n_eq = n_ineq = 0;
    for (i = 0; i < matrix->NbRows; ++i) {
        isl_mat **m;
        int row;

        if (SCOPVAL_zero_p(matrix->p[i][0])) {
            m = &eq;
            row = n_eq++;
        } else {
            m = &ineq;
            row = n_ineq++;
        }

        for (j = 0; j < matrix->NbColumns - 1; ++j) {
            int t = SCOPVAL_get_si(matrix->p[i][1 + j]);
            isl_int_set_si(v, t);
            *m = isl_mat_set_element(*m, row, j, v);
        }
    }

    isl_int_clear(v);

    bset = isl_basic_set_from_constraint_matrices(dim, eq, ineq,
            isl_dim_set, isl_dim_div, isl_dim_param, isl_dim_cst);
    return isl_set_from_basic_set(bset);
}


/* Convert a scoplib_matrix_list_p describing a union of domains
 * to an isl_set.
 */
static __isl_give isl_set *scoplib_matrix_list_to_isl_set(
        scoplib_matrix_list_p list, __isl_take isl_dim *dim)
{
    isl_set *set;

    set = isl_set_empty(isl_dim_copy(dim));
    for (; list; list = list->next) {
        isl_set *set_i;
        set_i = scoplib_matrix_to_isl_set(list->elt, isl_dim_copy(dim));
        set = isl_set_union(set, set_i);
    }

    isl_dim_free(dim);
    return set;
}

/* Convert an m x ( n + 1) pluto access_matrix_p [d A c]
 * to an m x (m + n + 1) isl_mat [-I A c].
 */
static __isl_give isl_mat *pluto_extract_equalities(isl_ctx *ctx,
        PlutoMatrix *matrix)
{
    int i, j;
    int n_col, n;
    isl_int v;
    isl_mat *eq;

    n_col = matrix->ncols;
    n = matrix->nrows;

    isl_int_init(v);
    eq = isl_mat_alloc(ctx, n, n + n_col);

    for (i = 0; i < n; ++i) {
        isl_int_set_si(v, 0);
        for (j = 0; j < n; ++j)
            eq = isl_mat_set_element(eq, i, j, v);
        isl_int_set_si(v, -1);
        eq = isl_mat_set_element(eq, i, i, v);
        for (j = 0; j < n_col ; ++j) {
            int t = SCOPVAL_get_si(matrix->val[i][j]);
            isl_int_set_si(v, t);
            eq = isl_mat_set_element(eq, i, n + j, v);
        }
    }

    isl_int_clear(v);

    return eq;
}

/* Convert an m x (1 + n + 1) scoplib_matrix_p [d A c]
 * to an m x (m + n + 1) isl_mat [-I A c].
 */
static __isl_give isl_mat *extract_equalities(isl_ctx *ctx,
        scoplib_matrix_p matrix, int first, int n)
{
    int i, j;
    int n_col;
    isl_int v;
    isl_mat *eq;

    n_col = matrix->NbColumns;

    isl_int_init(v);
    eq = isl_mat_alloc(ctx, n, n + n_col - 1);

    for (i = 0; i < n; ++i) {
        isl_int_set_si(v, 0);
        for (j = 0; j < n; ++j)
            eq = isl_mat_set_element(eq, i, j, v);
        isl_int_set_si(v, -1);
        eq = isl_mat_set_element(eq, i, i, v);
        for (j = 0; j < n_col - 1; ++j) {
            int t = SCOPVAL_get_si(matrix->p[first + i][1 + j]);
            isl_int_set_si(v, t);
            eq = isl_mat_set_element(eq, i, n + j, v);
        }
    }

    isl_int_clear(v);

    return eq;
}


/* Convert a scoplib_matrix_p schedule [0 A c] to
 * the isl_map { i -> A i + c } in the space prescribed by "dim".
 */
static __isl_give isl_map *scoplib_schedule_to_isl_map(
        scoplib_matrix_p schedule, __isl_take isl_dim *dim)
{
    int n_row, n_col;
    isl_ctx *ctx;
    isl_mat *eq, *ineq;
    isl_basic_map *bmap;

    ctx = isl_dim_get_ctx(dim);
    n_row = schedule->NbRows;
    n_col = schedule->NbColumns;

    ineq = isl_mat_alloc(ctx, 0, n_row + n_col - 1);
    eq = extract_equalities(ctx, schedule, 0, n_row);

    bmap = isl_basic_map_from_constraint_matrices(dim, eq, ineq,
            isl_dim_out, isl_dim_in, isl_dim_div, isl_dim_param, isl_dim_cst);
    return isl_map_from_basic_map(bmap);
}


/* Return the number of lines until the next non-zero element
 * in the first column of "access" or until the end of the matrix.
 */
static int access_len(scoplib_matrix_p access, int first)
{
    int i;

    for (i = first + 1; i < access->NbRows; ++i)
        if (!SCOPVAL_zero_p(access->p[i][0]))
            break;

    return i - first;
}


/* Convert a scoplib_matrix_p describing a series of accesses
 * to an isl_union_map with domain "dom" (in space "D").
 * Each access in "access" has a non-zero integer in the first column
 * of the first row identifying the array being accessed.  The remaining
 * entries of the first column are zero.
 * Let "A" be array identified by the first entry.
 * The remaining columns have the form [B c].
 * Each such access is converted to a map { D[i] -> A[B i + c] } * dom.
 *
 * Note that each access in the input is described by at least one row,
 * which means that there is no way of distinguishing between an access
 * to a scalar and an access to the first element of a 1-dimensional array.
 */
static __isl_give isl_union_map *scoplib_access_to_isl_union_map(
        scoplib_matrix_p access, __isl_take isl_set *dom, char **arrays)
{
    int i, len, n_col;
    isl_ctx *ctx;
    isl_dim *dim;
    isl_mat *eq, *ineq;
    isl_union_map *res;

    ctx = isl_set_get_ctx(dom);

    dim = isl_set_get_dim(dom);
    dim = isl_dim_drop(dim, isl_dim_set, 0, isl_dim_size(dim, isl_dim_set));
    res = isl_union_map_empty(dim);

    n_col = access->NbColumns;

    for (i = 0; i < access->NbRows; i += len) {
        isl_basic_map *bmap;
        isl_map *map;
        int arr = SCOPVAL_get_si(access->p[i][0]) - 1;

        len = access_len(access, i);

        dim = isl_set_get_dim(dom);
        dim = isl_dim_from_domain(dim);
        dim = isl_dim_add(dim, isl_dim_out, len);
        dim = isl_dim_set_tuple_name(dim, isl_dim_out, arrays[arr]);

        ineq = isl_mat_alloc(ctx, 0, len + n_col - 1);
        eq = extract_equalities(ctx, access, i, len);

        bmap = isl_basic_map_from_constraint_matrices(dim, eq, ineq,
                isl_dim_out, isl_dim_in, isl_dim_div, isl_dim_param, isl_dim_cst);
        map = isl_map_from_basic_map(bmap);
        map = isl_map_intersect_domain(map, isl_set_copy(dom));
        res = isl_union_map_union(res, isl_union_map_from_map(map));
    }

    isl_set_free(dom);

    return res;
}

/*
 * Like scoplib_access_to_isl_union_map, but just for a single scoplib access
 * (read or write)
 * pos: position (starting row) of the access in 'access'
 */
static __isl_give isl_map *scoplib_basic_access_to_isl_union_map(
        scoplib_matrix_p access, int pos, __isl_take isl_set *dom, 
        char **arrays)
{
    int len, n_col;
    isl_ctx *ctx;
    isl_dim *dim;
    isl_mat *eq, *ineq;

    ctx = isl_set_get_ctx(dom);

    dim = isl_set_get_dim(dom);
    dim = isl_dim_drop(dim, isl_dim_set, 0, isl_dim_size(dim, isl_dim_set));

    n_col = access->NbColumns;

    isl_basic_map *bmap;
    isl_map *map;
    int arr = SCOPVAL_get_si(access->p[pos][0]) - 1;

    len = access_len(access, pos);

    dim = isl_set_get_dim(dom);
    dim = isl_dim_from_domain(dim);
    dim = isl_dim_add(dim, isl_dim_out, len);
    dim = isl_dim_set_tuple_name(dim, isl_dim_out, arrays[arr]);

    ineq = isl_mat_alloc(ctx, 0, len + n_col - 1);
    eq = extract_equalities(ctx, access, pos, len);

    bmap = isl_basic_map_from_constraint_matrices(dim, eq, ineq,
            isl_dim_out, isl_dim_in, isl_dim_div, isl_dim_param, isl_dim_cst);
    map = isl_map_from_basic_map(bmap);
    map = isl_map_intersect_domain(map, dom);

    return map;
}

/*
 * Like scoplib_access_to_isl_union_map, but just for a single pluto access
 * (read or write)
 * pos: position (starting row) of the access in 'access'
 */
static __isl_give isl_map *pluto_basic_access_to_isl_union_map(
        PlutoMatrix  *mat, char* access_name,  __isl_take isl_set *dom)
{
    int len, n_col;
    isl_ctx *ctx;
    isl_dim *dim;
    isl_mat *eq, *ineq;

    ctx = isl_set_get_ctx(dom);

    dim = isl_set_get_dim(dom);
    dim = isl_dim_drop(dim, isl_dim_set, 0, isl_dim_size(dim, isl_dim_set));

    n_col = mat->ncols;

    isl_basic_map *bmap;
    isl_map *map;
    //int arr = SCOPVAL_get_si(access->p[pos][0]) - 1;

    len = mat->nrows;

    dim = isl_set_get_dim(dom);
    dim = isl_dim_from_domain(dim);
    dim = isl_dim_add(dim, isl_dim_out, len);
    dim = isl_dim_set_tuple_name(dim, isl_dim_out, access_name);

    ineq = isl_mat_alloc(ctx, 0, len + n_col);
    eq = pluto_extract_equalities(ctx, mat);

    bmap = isl_basic_map_from_constraint_matrices(dim, eq, ineq,
            isl_dim_out, isl_dim_in, isl_dim_div, isl_dim_param, isl_dim_cst);
    map = isl_map_from_basic_map(bmap);
    map = isl_map_intersect_domain(map, dom);

    return map;
}


static int basic_map_count(__isl_take isl_basic_map *bmap, void *user)
{
    int *count = user;

    *count += 1;
    isl_basic_map_free(bmap);
    return 0;
}


int isl_map_count(__isl_take isl_map *map, void *user)
{
    int r;

    r = isl_map_foreach_basic_map(map, &basic_map_count, user);
    isl_map_free(map);
    return r;
}


/* Temporary data structure used inside extract_deps.
 *
 * deps points to the array of Deps being constructed
 * type is the type of the next Dep
 * index is the index of the next Dep in the array.
 */
struct pluto_extra_dep_info {
    Dep **deps;
    Stmt **stmts;
    int type;
    int index;
};


/* Convert an isl_basic_map describing part of a dependence to a Dep.
 * The names of the input and output spaces are of the form S_d or S_d_e
 * with d an integer identifying the statement, e identifying the access
 * (relative to the statement). If it's of the form S_d_e and read/write
 * accesses for the statement are available, source and target accesses 
 * are set for the dependence, otherwise not.
 */
static int basic_map_extract(__isl_take isl_basic_map *bmap, void *user)
{
    Stmt **stmts;
    Dep *dep;
    struct pluto_extra_dep_info *info;
    info = (struct pluto_extra_dep_info *)user;

    stmts = info->stmts;

    bmap = isl_basic_map_remove_divs(bmap);

    dep = info->deps[info->index];

    dep->id = info->index;
    dep->dpolytope = isl_basic_map_to_pluto_constraints(bmap);
    dep->dirvec = NULL;
    dep->type = info->type;
    dep->src = atoi(isl_basic_map_get_tuple_name(bmap, isl_dim_in) + 2);
    dep->dest = atoi(isl_basic_map_get_tuple_name(bmap, isl_dim_out) + 2);

    // pluto_stmt_print(stdout, stmts[dep->src]);
    // pluto_stmt_print(stdout, stmts[dep->dest]);
    // printf("Src acc: %d dest acc: %d\n", src_acc_num, dest_acc_num);

    if (stmts[dep->src]->reads != NULL && stmts[dep->dest]->reads != NULL) {
        /* Extract access function information */
        int src_acc_num, dest_acc_num;
        const char *name;
        name = isl_basic_map_get_tuple_name(bmap, isl_dim_in) + 2;
        while (*name != '\0' && *(name++) != '_');
        if (*name != '\0') src_acc_num = atoi(name+1);
        else assert(0); // access function num not encoded in dependence

        name = isl_basic_map_get_tuple_name(bmap, isl_dim_out) + 2;
        while (*name != '\0' && *(name++) != '_');
        if (*name != '\0') dest_acc_num = atoi(name+1);
        else assert(0); // access function num not encoded in dependence

        switch (info->type) {
            case CANDL_RAW: 
                dep->src_acc = stmts[dep->src]->writes[src_acc_num];
                dep->dest_acc = stmts[dep->dest]->reads[dest_acc_num];
                break;
            case CANDL_WAW: 
                dep->src_acc = stmts[dep->src]->writes[src_acc_num];
                dep->dest_acc = stmts[dep->dest]->writes[dest_acc_num];
                break;
            case CANDL_WAR: 
                dep->src_acc = stmts[dep->src]->reads[src_acc_num];
                dep->dest_acc = stmts[dep->dest]->writes[dest_acc_num];
                break;
            case CANDL_RAR: 
                dep->src_acc = stmts[dep->src]->reads[src_acc_num];
                dep->dest_acc = stmts[dep->dest]->reads[dest_acc_num];
                break;
            default:
                assert(0);
        }
    }else{
        dep->src_acc = NULL;
        dep->dest_acc = NULL;
    }

    info->index++;
    isl_basic_map_free(bmap);
    return 0;
}


static int map_extract(__isl_take isl_map *map, void *user)
{
    int r;

    r = isl_map_foreach_basic_map(map, &basic_map_extract, user);
    isl_map_free(map);
    return r;
}


int extract_deps(Dep **deps, int first, Stmt **stmts,
        __isl_keep isl_union_map *umap, int type)
{
    struct pluto_extra_dep_info info = { deps, stmts, type, first };

    isl_union_map_foreach_map(umap, &map_extract, &info);

    return info.index - first;
}


/* Compute dependences based on the iteration domain and access
 * information in "scop" and put the result in "prog".
 *
 * If options->lastwriter is false, then
 *      RAW deps are those from any earlier write to a read
 *      WAW deps are those from any earlier write to a write
 *      WAR deps are those from any earlier read to a write
 *      RAR deps are those from any earlier read to a read
 * If options->lastwriter is true, then
 *      RAW deps are those from the last write to a read
 *      WAW deps are those from the last write to a write
 *      WAR deps are those from any earlier read not masked by an intermediate
 *      write to a write
 *      RAR deps are those from the last read to a read
 *
 * The RAR deps are only computed if options->rar is set.
 */
static void compute_deps(scoplib_scop_p scop, PlutoProg *prog,
        PlutoOptions *options)
{
    int i, racc_num, wacc_num, pos, len;
    int nstmts = scoplib_statement_number(scop->statement);
    isl_ctx *ctx;
    isl_dim *dim;
    isl_space *param_space;
    isl_set *context;
    isl_union_map *empty;
    isl_union_map *write;
    isl_union_map *read;
    isl_union_map *schedule;
    isl_union_map *dep_raw, *dep_war, *dep_waw, *dep_rar, *trans_dep_war;
    isl_union_map *trans_dep_waw;
    scoplib_statement_p stmt;

    ctx = isl_ctx_alloc();
    assert(ctx);

    dim = isl_dim_set_alloc(ctx, scop->nb_parameters, 0);
    dim = set_names(dim, isl_dim_param, scop->parameters);
    param_space = isl_space_params(isl_space_copy(dim));
    context = scoplib_matrix_to_isl_set(scop->context, param_space);

    if (!options->rar) dep_rar = isl_union_map_empty(isl_dim_copy(dim));
    empty = isl_union_map_empty(isl_dim_copy(dim));
    write = isl_union_map_empty(isl_dim_copy(dim));
    read = isl_union_map_empty(isl_dim_copy(dim));
    schedule = isl_union_map_empty(dim);

    if (options->isldepcompact) {
        /* Leads to fewer dependences. Each dependence may not have a unique
         * source/target access relating to it, since a union is taken
         * across all reads for a statement (and writes) for a particualr
         * array. Relationship between a dependence and associated dependent
         * data / array elements is lost, and some analyses may not work with
         * such a representation */
        for (i = 0, stmt = scop->statement; i < nstmts; ++i, stmt = stmt->next) {
            isl_set *dom;
            isl_map *schedule_i;
            isl_union_map *read_i;
            isl_union_map *write_i;
            char name[20];

            snprintf(name, sizeof(name), "S_%d", i);

            dim = isl_dim_set_alloc(ctx, scop->nb_parameters, stmt->nb_iterators);
            dim = set_names(dim, isl_dim_param, scop->parameters);
            dim = set_names(dim, isl_dim_set, stmt->iterators);
            dim = isl_dim_set_tuple_name(dim, isl_dim_set, name);
            dom = scoplib_matrix_list_to_isl_set(stmt->domain, dim);
            dom = isl_set_intersect_params(dom, isl_set_copy(context));

            dim = isl_dim_alloc(ctx, scop->nb_parameters, stmt->nb_iterators,
                    2 * stmt->nb_iterators + 1);
            dim = set_names(dim, isl_dim_param, scop->parameters);
            dim = set_names(dim, isl_dim_in, stmt->iterators);
            dim = isl_dim_set_tuple_name(dim, isl_dim_in, name);
            schedule_i = scoplib_schedule_to_isl_map(stmt->schedule, dim);

            read_i = scoplib_access_to_isl_union_map(stmt->read, isl_set_copy(dom),
                    scop->arrays);
            write_i = scoplib_access_to_isl_union_map(stmt->write, dom,
                    scop->arrays);

            read = isl_union_map_union(read, read_i);
            write = isl_union_map_union(write, write_i);
            schedule = isl_union_map_union(schedule,
                    isl_union_map_from_map(schedule_i));
        }
    }else{
        /* Each dependence is for a particular source and target access. Use
         * <stmt, access> pair while relating to accessed data so each
         * dependence can be associated to a unique source and target access
         */
        for (i = 0, stmt = scop->statement; i < nstmts; ++i, stmt = stmt->next) {
            isl_set *dom;

            racc_num = 0;
            wacc_num = 0;

            for (pos = 0; pos < stmt->read->NbRows + stmt->write->NbRows; pos += len) {
                isl_map *read_pos;
                isl_map *write_pos;
                isl_map *schedule_i;

                char name[20];

                if (pos<stmt->read->NbRows) {
                    snprintf(name, sizeof(name), "S_%d_r%d", i, racc_num);
                }else{
                    snprintf(name, sizeof(name), "S_%d_w%d", i, wacc_num);
                }

                dim = isl_dim_set_alloc(ctx, scop->nb_parameters, stmt->nb_iterators);
                dim = set_names(dim, isl_dim_param, scop->parameters);
                dim = set_names(dim, isl_dim_set, stmt->iterators);
                dim = isl_dim_set_tuple_name(dim, isl_dim_set, name);
                dom = scoplib_matrix_list_to_isl_set(stmt->domain, dim);
                dom = isl_set_intersect_params(dom, isl_set_copy(context));

                dim = isl_dim_alloc(ctx, scop->nb_parameters, stmt->nb_iterators,
                        2 * stmt->nb_iterators + 1);
                dim = set_names(dim, isl_dim_param, scop->parameters);
                dim = set_names(dim, isl_dim_in, stmt->iterators);
                dim = isl_dim_set_tuple_name(dim, isl_dim_in, name);

                schedule_i = scoplib_schedule_to_isl_map(stmt->schedule, dim);

                if (pos<stmt->read->NbRows) {
                    len = access_len(stmt->read, pos);
                }else{
                    len = access_len(stmt->write, pos - stmt->read->NbRows);
                }

                if (pos<stmt->read->NbRows) {
                    read_pos = scoplib_basic_access_to_isl_union_map(stmt->read, 
                            pos, dom, scop->arrays);
                    read = isl_union_map_union(read, isl_union_map_from_map(read_pos));
                }else{
                    write_pos = scoplib_basic_access_to_isl_union_map(stmt->write, 
                            pos-stmt->read->NbRows, dom, scop->arrays);
                    write = isl_union_map_union(write, isl_union_map_from_map(write_pos));
                }

                schedule = isl_union_map_union(schedule,
                        isl_union_map_from_map(schedule_i));
                if (pos<stmt->read->NbRows) {
                    racc_num++;
                }else{
                    wacc_num++;
                }
            }
        }
    }

    if (options->lastwriter) {
        // compute RAW dependences which do not contain transitive dependences
        isl_union_map_compute_flow(isl_union_map_copy(read),
                isl_union_map_copy(write),
                isl_union_map_copy(empty),
                isl_union_map_copy(schedule),
                &dep_raw, NULL, NULL, NULL);
        // compute WAW and WAR dependences which do not contain transitive dependences
        isl_union_map_compute_flow(isl_union_map_copy(write),
                isl_union_map_copy(write),
                isl_union_map_copy(read),
                isl_union_map_copy(schedule),
                &dep_waw, &dep_war, NULL, NULL);
        if (options->distmem) {
            // compute WAR dependences which may contain transitive dependences
            isl_union_map_compute_flow(isl_union_map_copy(write),
                    isl_union_map_copy(empty),
                    isl_union_map_copy(read),
                    isl_union_map_copy(schedule),
                    NULL, &trans_dep_war, NULL, NULL);
            isl_union_map_compute_flow(isl_union_map_copy(write),
                    isl_union_map_copy(empty),
                    isl_union_map_copy(write),
                    isl_union_map_copy(schedule),
                    NULL, &trans_dep_waw, NULL, NULL);
        }
        if (options->rar) {
            // compute RAR dependences which do not contain transitive dependences
            isl_union_map_compute_flow(isl_union_map_copy(read),
                    isl_union_map_copy(read),
                    isl_union_map_copy(empty),
                    isl_union_map_copy(schedule),
                    &dep_rar, NULL, NULL, NULL);
        }
    }else{
        // compute RAW dependences which may contain transitive dependences
        isl_union_map_compute_flow(isl_union_map_copy(read),
                isl_union_map_copy(empty),
                isl_union_map_copy(write),
                isl_union_map_copy(schedule),
                NULL, &dep_raw, NULL, NULL);
        // compute WAR dependences which may contain transitive dependences
        isl_union_map_compute_flow(isl_union_map_copy(write),
                isl_union_map_copy(empty),
                isl_union_map_copy(read),
                isl_union_map_copy(schedule),
                NULL, &dep_war, NULL, NULL);
        // compute WAW dependences which may contain transitive dependences
        isl_union_map_compute_flow(isl_union_map_copy(write),
                isl_union_map_copy(empty),
                isl_union_map_copy(write),
                isl_union_map_copy(schedule),
                NULL, &dep_waw, NULL, NULL);
        if (options->rar) {
            // compute RAR dependences which may contain transitive dependences
            isl_union_map_compute_flow(isl_union_map_copy(read),
                    isl_union_map_copy(empty),
                    isl_union_map_copy(read),
                    isl_union_map_copy(schedule),
                    NULL, &dep_rar, NULL, NULL);
        }
    }

    dep_raw = isl_union_map_coalesce(dep_raw);
    dep_war = isl_union_map_coalesce(dep_war);
    dep_waw = isl_union_map_coalesce(dep_waw);
    dep_rar = isl_union_map_coalesce(dep_rar);

    prog->ndeps = 0;
    isl_union_map_foreach_map(dep_raw, &isl_map_count, &prog->ndeps);
    isl_union_map_foreach_map(dep_war, &isl_map_count, &prog->ndeps);
    isl_union_map_foreach_map(dep_waw, &isl_map_count, &prog->ndeps);
    isl_union_map_foreach_map(dep_rar, &isl_map_count, &prog->ndeps);

    prog->deps = (Dep **)malloc(prog->ndeps * sizeof(Dep *));
    for (i=0; i<prog->ndeps; i++) {
        prog->deps[i] = pluto_dep_alloc();
    }
    prog->ndeps = 0;
    prog->ndeps += extract_deps(prog->deps, prog->ndeps, prog->stmts, dep_raw, CANDL_RAW);
    prog->ndeps += extract_deps(prog->deps, prog->ndeps, prog->stmts, dep_war, CANDL_WAR);
    prog->ndeps += extract_deps(prog->deps, prog->ndeps, prog->stmts, dep_waw, CANDL_WAW);
    prog->ndeps += extract_deps(prog->deps, prog->ndeps, prog->stmts, dep_rar, CANDL_RAR);

    if (options->lastwriter) {
        trans_dep_war = isl_union_map_coalesce(trans_dep_war);
        trans_dep_waw = isl_union_map_coalesce(trans_dep_waw);

        prog->ntransdeps = 0;
        isl_union_map_foreach_map(dep_raw, &isl_map_count, &prog->ntransdeps);
        isl_union_map_foreach_map(trans_dep_war, &isl_map_count, &prog->ntransdeps);
        isl_union_map_foreach_map(trans_dep_waw, &isl_map_count, &prog->ntransdeps);
        isl_union_map_foreach_map(dep_rar, &isl_map_count, &prog->ntransdeps);

        if (prog->ntransdeps >= 1) {
            prog->transdeps = (Dep **)malloc(prog->ntransdeps * sizeof(Dep *));
            for (i=0; i<prog->ntransdeps; i++) {
                prog->transdeps[i] = pluto_dep_alloc();
            }
            prog->ntransdeps = 0;
            prog->ntransdeps += extract_deps(prog->transdeps, prog->ntransdeps, prog->stmts, dep_raw, CANDL_RAW);
            prog->ntransdeps += extract_deps(prog->transdeps, prog->ntransdeps, prog->stmts, trans_dep_war, CANDL_WAR);
            prog->ntransdeps += extract_deps(prog->transdeps, prog->ntransdeps, prog->stmts, trans_dep_waw, CANDL_WAW);
            prog->ntransdeps += extract_deps(prog->transdeps, prog->ntransdeps, prog->stmts, dep_rar, CANDL_RAR);
        }

        isl_union_map_free(trans_dep_war);
        isl_union_map_free(trans_dep_waw);
    }

    isl_union_map_free(dep_raw);
    isl_union_map_free(dep_war);
    isl_union_map_free(dep_waw);
    isl_union_map_free(dep_rar);

    isl_union_map_free(empty);
    isl_union_map_free(write);
    isl_union_map_free(read);
    isl_union_map_free(schedule);
    isl_set_free(context);

    isl_ctx_free(ctx);
}


scoplib_matrix_p get_identity_schedule(int dim, int npar){
    scoplib_matrix_p smat = scoplib_matrix_malloc(2*dim+1, dim+npar+1+1);

    int i, j;
    for(i =0; i<2*dim+1; i++)
        for(j=0; j<dim+1+npar+1; j++)
            smat->p[i][j] = 0;

    for(i=1; i<dim; i++)
        smat->p[2*i-1][i] = 1;

    return smat;

}

/*
 * Computes the dependence polyhedron between the source iterators of dep1 and dep2
 * domain1:  source iterators of dep1
 * domain2:  source iterators of dep2
 * dep1: first dependence
 * dep2: second dependence
 * access_matrix: access function matrix for source iterators of dep2. pass NULL to use access function in dep2
 * returns dependence polyhedron
 */

PlutoConstraints* pluto_find_dependence(PlutoConstraints *domain1, PlutoConstraints *domain2, Dep *dep1, Dep *dep2,
        PlutoProg *prog, PlutoMatrix *access_matrix)
{
    int i, *divs;
    isl_ctx *ctx;
    isl_dim *dim;
    isl_space *param_space;
    isl_set *context;
    isl_union_map *empty;
    isl_union_map *write;
    isl_union_map *read;
    isl_union_map *schedule;
    isl_union_map *dep_raw;

    ctx = isl_ctx_alloc();
    assert(ctx);

    dim = isl_dim_set_alloc(ctx, prog->npar , 0);
    dim = set_names(dim, isl_dim_param,prog->params );
    param_space = isl_space_params(isl_space_copy(dim));
    context = scoplib_matrix_to_isl_set(pluto_constraints_to_scoplib_matrix(prog->context), param_space);

    empty = isl_union_map_empty(isl_dim_copy(dim));
    write = isl_union_map_empty(isl_dim_copy(dim));
    read = isl_union_map_empty(isl_dim_copy(dim));
    schedule = isl_union_map_empty(dim);

    isl_set *dom;

    //Add the source iterators of dep1 and corresponding access function to isl
    PlutoConstraints *source_iterators = domain2;
    PlutoAccess *access = dep2->src_acc;
    Stmt *s = prog->stmts[dep2->src];
    int domain_dim = source_iterators->ncols - prog->npar -1;
    char **iter = (char**)malloc(domain_dim*sizeof(char*));

    for (i=0; i < domain_dim; i++) {
        iter[i] = malloc(10 * sizeof(char));
        sprintf(iter[i], "d%d", i+1);
    }

    //assert(domain_dim <= s->dim);
    isl_map *read_pos;
    isl_map *write_pos;
    isl_map *schedule_i;

    char name[20];

    snprintf(name, sizeof(name), "S_%d_r%d", 0, 0);


    dim = isl_dim_set_alloc(ctx,prog->npar ,domain_dim );
    dim = set_names(dim, isl_dim_param,prog->params);
    dim = set_names(dim, isl_dim_set,iter);
    dim = isl_dim_set_tuple_name(dim, isl_dim_set, name);
    dom = scoplib_matrix_list_to_isl_set(pluto_constraints_list_to_scoplib_matrix_list(source_iterators), dim);
    dom = isl_set_intersect_params(dom, isl_set_copy(context));


    dim = isl_dim_alloc(ctx,prog->npar ,domain_dim,
            2*domain_dim +1);
    dim = set_names(dim, isl_dim_param, prog->params);
    dim = set_names(dim, isl_dim_in,iter );
    dim = isl_dim_set_tuple_name(dim, isl_dim_in, name);

    scoplib_matrix_p smat = get_identity_schedule(domain_dim, prog->npar);
    smat->p[0][smat->NbColumns-1] = 1;
    schedule_i = scoplib_schedule_to_isl_map(smat, dim);
    if(access_matrix == NULL)
		read_pos = pluto_basic_access_to_isl_union_map(pluto_get_new_access_func(s, access->mat, &divs),access->name,  dom);
    else
		read_pos = pluto_basic_access_to_isl_union_map(access_matrix,access->name,  dom);
    read = isl_union_map_union(read, isl_union_map_from_map(read_pos));

    schedule = isl_union_map_union(schedule,
            isl_union_map_from_map(schedule_i));


    for(i=0;i<domain_dim; i++){
        free(iter[i]);
    }

    free(iter);


    //Add the source iterators of dep2 and corresponding access function to isl
    source_iterators = domain1;
    access = dep1->src_acc;
    s = prog->stmts[dep1->src];
    domain_dim = source_iterators->ncols - prog->npar -1;

    iter = (char**)malloc(domain_dim*sizeof(char*));

    for (i=0; i < domain_dim; i++) {
        iter[i] = malloc(10 * sizeof(char));
        sprintf(iter[i], "d%d", i+1);
    }

    snprintf(name, sizeof(name), "S_%d_w%d", 0, 0);

    dim = isl_dim_set_alloc(ctx,prog->npar , domain_dim );
    dim = set_names(dim, isl_dim_param,prog->params);
    dim = set_names(dim, isl_dim_set,iter);
    dim = isl_dim_set_tuple_name(dim, isl_dim_set, name);
    dom = scoplib_matrix_list_to_isl_set(pluto_constraints_list_to_scoplib_matrix_list(source_iterators), dim);
    dom = isl_set_intersect_params(dom, isl_set_copy(context));

    dim = isl_dim_alloc(ctx,prog->npar ,domain_dim,
            2*domain_dim +1);
    dim = set_names(dim, isl_dim_param, prog->params);
    dim = set_names(dim, isl_dim_in,iter);
    dim = isl_dim_set_tuple_name(dim, isl_dim_in, name);

    scoplib_matrix_free(smat);
    smat = get_identity_schedule(domain_dim, prog->npar);
    schedule_i = scoplib_schedule_to_isl_map(smat, dim);

    write_pos = pluto_basic_access_to_isl_union_map(pluto_get_new_access_func(s, access->mat, &divs), access->name,  dom);
    //write_pos = pluto_basic_access_to_isl_union_map(access_matrix,access->name,  dom);
    write = isl_union_map_union(write, isl_union_map_from_map(write_pos));

    schedule = isl_union_map_union(schedule,
            isl_union_map_from_map(schedule_i));
    isl_union_map_compute_flow(isl_union_map_copy(read),
            isl_union_map_copy(empty),
            isl_union_map_copy(write),
            isl_union_map_copy(schedule),
            NULL, &dep_raw, NULL, NULL);


    /*
    //Find dep with last writer option
    isl_union_map_compute_flow(isl_union_map_copy(read),
    isl_union_map_copy(write),
    isl_union_map_copy(empty),
    isl_union_map_copy(schedule),
    &dep_raw, NULL, NULL, NULL);
    */

    dep_raw = isl_union_map_coalesce(dep_raw);

    int ndeps = 0;
    isl_union_map_foreach_map(dep_raw, &isl_map_count, &ndeps);

    if(ndeps == 0) {
        return NULL;
    }

    Dep **deps = (Dep **)malloc(ndeps * sizeof(Dep *));
    for (i=0; i<ndeps; i++) {
        deps[i] = pluto_dep_alloc();
    }
    ndeps = 0;
    ndeps += extract_deps(deps, ndeps, prog->stmts, dep_raw, CANDL_RAW);

    PlutoConstraints *tdpoly = NULL;
    for(i=0; i<ndeps; i++){
        if(tdpoly == NULL)
            tdpoly = pluto_constraints_dup(deps[i]->dpolytope);
        else
            pluto_constraints_unionize(tdpoly, deps[i]->dpolytope);
    }



    //TODO: Free deps

    isl_union_map_free(dep_raw);

    isl_union_map_free(empty);
    isl_union_map_free(write);
    isl_union_map_free(read);
    isl_union_map_free(schedule);
    isl_set_free(context);

    isl_ctx_free(ctx);

    for(i=0;i<domain_dim; i++){
        free(iter[i]);
    }

    free(iter);

    return tdpoly;
}

/* 
 * FIXME: should be called on identity schedules (in normal 2*d+1 form)
 *
 * For dependences on the original loop nest (with identity
 * transformation), we expect a dependence to be completely satisfied at
 * some level; they'll have a component of zero for all levels up to the level at
 * which they are satisfied; so if a loop is forced parallel, removing all
 * dependences satisfied at that level will lead to the loop being
 * detected as parallel 
 * depth: 0-indexed depth to be forced parallel
 * */
void pluto_force_parallelize(PlutoProg *prog, int depth) 
{
    int i, j;

    pluto_detect_transformation_properties(prog);
    if (options->lastwriter) {
        /* Add transitive edges that weren't included */
        int num_new_deps = prog->ndeps;
        while (num_new_deps > 0) {
            int first_new_dep = prog->ndeps - num_new_deps;
            num_new_deps = 0;
            for (i=first_new_dep; i<prog->ndeps; i++) {
                if (prog->deps[i]->satisfaction_level < 2*depth-1) {
                    for (j=0; j<prog->ndeps; j++) {
                        if (prog->deps[j]->satisfaction_level == 2*depth-1
                                && prog->deps[i]->dest_acc == prog->deps[j]->src_acc) {
                            Dep *dep = pluto_dep_compose(prog->deps[i], prog->deps[j], prog);
                            if (dep == NULL) continue;
                            dep->satisfaction_level = prog->deps[i]->satisfaction_level;
                            dep->satisfied = true;
                            switch(prog->deps[i]->type) {
                                case CANDL_WAR:
                                    if (IS_RAW(prog->deps[j]->type)) {
                                        dep->type = CANDL_RAR;
                                    }else{ // IS_WAW(prog->deps[j]->type)
                                        dep->type = CANDL_WAR;
                                    }
                                    break;
                                case CANDL_RAW:
                                    if (IS_RAR(prog->deps[j]->type)) {
                                        dep->type = CANDL_RAW;
                                    }else{ // IS_WAR(prog->deps[j]->type)
                                        dep->type = CANDL_WAW;
                                    }
                                    break;
                                case CANDL_WAW:
                                case CANDL_RAR:
                                    dep->type = prog->deps[j]->type;
                                    break;
                                default:
                                    assert(0);
                            }
                            pluto_add_dep(prog, dep);
                            /* printf("Adding transitive edge\n"); */
                            num_new_deps++;
                        }
                    }
                }
            }
        }

        if (!options->rar) { // remove RAR dependences that were added
            Dep **nonrardeps = (Dep **) malloc(sizeof(Dep *)*prog->ndeps);
            int count = 0;
            for (i=0; i<prog->ndeps; i++) {
                if (!IS_RAR(prog->deps[i]->type)) {
                    prog->deps[i]->id = count;
                    nonrardeps[count++] = prog->deps[i];
                }else{
                    // printf("removing edge\n");
                    pluto_dep_free(prog->deps[i]);
                }
            }
            free(prog->deps);
            prog->deps = nonrardeps;
            prog->ndeps = count;
        }
    }

    Dep **rdeps = (Dep **) malloc(sizeof(Dep *)*prog->ndeps);
    int count = 0;
    for (i=0; i<prog->ndeps; i++) {
        if (prog->deps[i]->satisfaction_level != 2*depth-1) {
            prog->deps[i]->id = count;
            rdeps[count++] = prog->deps[i];
        }else{
            // printf("removing edge\n");
            pluto_dep_free(prog->deps[i]);
        }
    }
    free(prog->deps);
    prog->deps = rdeps;
    prog->ndeps = count;

    Dep **rtransdeps = (Dep **) malloc(sizeof(Dep *)*prog->ntransdeps);
    count = 0;
    for (i=0; i<prog->ntransdeps; i++) {
        if (prog->transdeps[i]->satisfaction_level != 2*depth-1) {
            prog->transdeps[i]->id = count;
            rtransdeps[count++] = prog->transdeps[i];
        }else{
            // printf("removing edge\n");
            pluto_dep_free(prog->transdeps[i]);
        }
    }
    free(prog->transdeps);
    prog->transdeps = rtransdeps;
    prog->ntransdeps = count;
}


/* 
 * Extract necessary information from clan_scop to create PlutoProg - a
 * representation of the program sufficient to be used throughout Pluto. 
 * PlutoProg also includes dependences; so candl is run here.
 */
PlutoProg *scop_to_pluto_prog(scoplib_scop_p scop, PlutoOptions *options)
{
    int i, max_sched_rows;

    PlutoProg *prog = pluto_prog_alloc();

    prog->nstmts = scoplib_statement_number(scop->statement);
    prog->options = options;

    /* Data variables in the program */
    scoplib_symbol_p symbol;
    prog->num_data = 0;
    for(symbol = scop->symbol_table;symbol;symbol = symbol->next) {
        prog->num_data++;
    }
    prog->data_names = (char **) malloc (prog->num_data * sizeof(char *));
    prog->num_data = 0;
    for(symbol = scop->symbol_table;symbol;symbol = symbol->next) {
        prog->data_names[prog->num_data++] = strdup(symbol->identifier);
    }

    /* Program parameters */
    prog->npar = scop->nb_parameters;

    if (prog->npar >= 1)    {
        prog->params = (char **) malloc(sizeof(char *)*prog->npar);
    }
    for (i=0; i<prog->npar; i++)  {
        prog->params[i] = strdup(scop->parameters[i]);
    }

    pluto_constraints_free(prog->context);
    prog->context = scoplib_matrix_to_pluto_constraints(scop->context);

    if (options->context != -1)	{
        for (i=0; i<prog->npar; i++)  {
            pluto_constraints_add_inequality(prog->context);
            prog->context->val[i][i] = 1;
            prog->context->val[i][prog->context->ncols-1] = -options->context;
        }
    }

    scoplib_statement_p scop_stmt = scop->statement;

    prog->nvar = scop_stmt->nb_iterators;
    max_sched_rows = 0;
    for (i=0; i<prog->nstmts; i++) {
        prog->nvar = PLMAX(prog->nvar, scop_stmt->nb_iterators);
        max_sched_rows = PLMAX(max_sched_rows, scop_stmt->schedule->NbRows);
        scop_stmt = scop_stmt->next;
    }

    prog->stmts = scoplib_to_pluto_stmts(scop);
    prog->scop = scop;

    /* Compute dependences */
    if (options->isldep) {
        compute_deps(scop, prog, options);
    }else{
        /*  Using Candl */
        candl_program_p candl_program = candl_program_convert_scop(scop, NULL);

        CandlOptions *candlOptions = candl_options_malloc();
        if (options->rar)   {
            candlOptions->rar = 1;
        }
        candlOptions->lastwriter = options->lastwriter;
        candlOptions->scalar_privatization = options->scalpriv;
        // candlOptions->verbose = 1;

        CandlDependence *candl_deps = candl_dependence(candl_program,
                candlOptions);
        prog->deps = deps_read(candl_deps, prog);
        prog->ndeps = candl_num_dependences(candl_deps);
        candl_options_free(candlOptions);
        candl_dependence_free(candl_deps);
        candl_program_free(candl_program);

        prog->transdeps = NULL;
        prog->ntransdeps = 0;
    }

    /* Add hyperplanes */
    if (prog->nstmts >= 1) {
        for (i=0; i<max_sched_rows; i++) {
            pluto_prog_add_hyperplane(prog,prog->num_hyperplanes,H_UNKNOWN);
            prog->hProps[prog->num_hyperplanes-1].type = 
                (i%2)? H_LOOP: H_SCALAR;
        }
    }

    /* Hack for linearized accesses */
    FILE *lfp = fopen(".linearized", "r");
    FILE *nlfp = fopen(".nonlinearized", "r");
    char tmpstr[256];
    char linearized[256];
    if (lfp && nlfp) {
        for (i=0; i<prog->nstmts; i++)    {
            rewind(lfp);
            rewind(nlfp);
            while (!feof(lfp) && !feof(nlfp))      {
                fgets(tmpstr, 256, nlfp);
                fgets(linearized, 256, lfp);
                if (strstr(tmpstr, prog->stmts[i]->text))        {
                    prog->stmts[i]->text = (char *) realloc(prog->stmts[i]->text, sizeof(char)*(strlen(linearized)+1));
                    strcpy(prog->stmts[i]->text, linearized);
                }
            }
        }
        fclose(lfp);
        fclose(nlfp);
    }

    if (options->forceparallel >= 1) {
        // forceparallel support only for 6 dimensions
        // force parallellize dimension-by-dimension, from innermost to outermost
        if (options->forceparallel & 32) {
            pluto_force_parallelize(prog, 6);
        }
        if (options->forceparallel & 16) {
            pluto_force_parallelize(prog, 5);
        }
        if (options->forceparallel & 8) {
            pluto_force_parallelize(prog, 4);
        }
        if (options->forceparallel & 4) {
            pluto_force_parallelize(prog, 3);
        }
        if (options->forceparallel & 2) {
            pluto_force_parallelize(prog, 2);
        }
        if (options->forceparallel & 1) {
            pluto_force_parallelize(prog, 1);
        }
    }

    return prog;
}

/* Get an upper bound for transformation coefficients to prevent spurious
 * transformations that represent shifts or skews proportional to trip counts:
 * this happens when loop bounds are constants
 */
int get_coeff_upper_bound(PlutoProg *prog)
{
    int max, i, r;

    max = 0;
    for (i=0; i<prog->nstmts; i++)  {
        Stmt *stmt = prog->stmts[i];
        for (r=0; r<stmt->domain->nrows; r++) {
            max  = PLMAX(max,stmt->domain->val[r][stmt->domain->ncols-1]);
        }
    }

    return max-1;
}


PlutoProg *pluto_prog_alloc()
{
    PlutoProg *prog = (PlutoProg *) malloc(sizeof(PlutoProg));

    prog->nstmts = 0;
    prog->stmts = NULL;
    prog->npar = 0;
    prog->nvar = 0;
    prog->params = NULL;
    prog->context = pluto_constraints_alloc(1, prog->npar+1);
    prog->deps = NULL;
    prog->ndeps = 0;
    prog->transdeps = NULL;
    prog->ntransdeps = 0;
    prog->ddg = NULL;
    prog->hProps = NULL;
    prog->num_hyperplanes = 0;
    prog->decls = malloc(16384*9);

    strcpy(prog->decls, "");

    prog->globcst = NULL;
    prog->depcst = NULL;

    return prog;
}



void pluto_prog_free(PlutoProg *prog)
{
    int i;

    /* Free dependences */
    for (i=0; i<prog->ndeps; i++) {
        pluto_dep_free(prog->deps[i]);
    }
    if (prog->deps != NULL) {
        free(prog->deps);
    }

    /* Free DDG */
    if (prog->ddg != NULL)  {
        graph_free(prog->ddg);
    }

    if (prog->hProps != NULL)   {
        free(prog->hProps);
    }

    for (i=0; i<prog->npar; i++)  {
        free(prog->params[i]);
    }
    if (prog->npar >= 1)    {
        free(prog->params);
    }

    /* Statements */
    for (i=0; i<prog->nstmts; i++) {
        pluto_stmt_free(prog->stmts[i]);
    }
    if (prog->nstmts >= 1)  {
        free(prog->stmts);
    }

    pluto_constraints_free(prog->context);
    if (prog->depcst != NULL) {
        pluto_constraints_free(*prog->depcst);
    }
    free(prog->depcst);
    pluto_constraints_free(prog->globcst);

    free(prog->decls);

    free(prog);
}


PlutoOptions *pluto_options_alloc()
{
    PlutoOptions *options;

    options  = (PlutoOptions *) malloc(sizeof(PlutoOptions));

    /* Initialize to default */
    options->tile = 0;
    options->intratileopt = 1;
    options->dynschedule = 0;
    options->debug = 0;
    options->moredebug = 0;
    options->scancount = 0;
    options->parallel = 0;
    options->innerpar = 0;
    options->identity = 0;

    /* Distmem parallelization options */
    options->distmem = 0;

#ifdef PLUTO_OPENCL
    options->opencl = 0;
#endif

    options->commopt = 1;
    options->commopt_fop = 0;
    options->fop_unicast_runtime = 0;
    options->commopt_foifi = 0;
    options->commreport = 0;
    options->variables_not_global = 0;
    /* Can be bad for performance (overhead in its absence acceptable) */
    options->fusesends = 0;
    options->mpiomp = 0;
    options->blockcyclic = 0;
    options->cyclesize = 32;

    options->unroll = 0;

    /* Unroll/jam factor */
    options->ufactor = 8;

    /* Ignore input deps */
    options->rar = 0;

    /* Override for first and last levels to tile */
    options->ft = -1;
    options->lt = -1;

    /* Override for first and last cloog options */
    options->cloogf = -1;
    options->cloogl = -1;

    options->cloogsh = 0;

    options->cloogbacktrack = 1;

    options->multipipe = 0;
    options->l2tile = 0;
    options->prevector = 1;
    options->fuse = SMART_FUSE;

    /* Experimental */
    options->polyunroll = 0;

    /* Default context is no context */
    options->context = -1;

    options->forceparallel = 0;

    options->bee = 0;

    options->isldep = 0;
    options->isldepcompact = 0;

    options->islsolve = 0;

    options->readscoplib = 0;

    options->lastwriter = 0;

    options->nobound = 0;

    options->scalpriv = 0;

    options->silent = 0;

    options->out_file = NULL;

    return options;
}


/* Add global/program parameter at position 'pos' */
void pluto_prog_add_param(PlutoProg *prog, const char *param, int pos)
{
    int i, j;

    for (i=0; i<prog->nstmts; i++) {
        Stmt *stmt = prog->stmts[i];
        pluto_constraints_add_dim(stmt->domain, stmt->domain->ncols-1-prog->npar+pos);
        pluto_matrix_add_col(stmt->trans, stmt->trans->ncols-1-prog->npar+pos);

        for (j=0; j<stmt->nwrites; j++)  {
            pluto_matrix_add_col(stmt->writes[j]->mat, stmt->dim+pos);
        }
        for (j=0; j<stmt->nreads; j++)  {
            pluto_matrix_add_col(stmt->reads[j]->mat, stmt->dim+pos);
        }
    }
    for (i=0; i<prog->ndeps; i++)   {
        pluto_constraints_add_dim(prog->deps[i]->dpolytope, 
                prog->deps[i]->dpolytope->ncols-1-prog->npar+pos);
    }
    pluto_constraints_add_dim(prog->context, prog->context->ncols-1-prog->npar+pos);

    prog->params = (char **) realloc(prog->params, sizeof(char *)*(prog->npar+1));

    for (i=prog->npar-1; i>=pos; i--)    {
        prog->params[i+1] = prog->params[i];
    }

    prog->params[pos] = strdup(param);
    prog->npar++;
}


void pluto_options_free(PlutoOptions *options)
{
    if (options->out_file != NULL)  {
        free(options->out_file);
    }
    free(options);
}


/* pos: position of domain iterator 
 * time_pos: position of time iterator; iter: domain iterator; supply -1
 * if you don't want a scattering function row added for it */
void pluto_stmt_add_dim(Stmt *stmt, int pos, int time_pos, const char *iter,
        PlutoHypType hyp_type, PlutoProg *prog)
{
    int i, npar;

    npar = stmt->domain->ncols - stmt->dim - 1;

    assert(pos <= stmt->dim);
    assert(time_pos <= stmt->trans->nrows);
    assert(stmt->dim + npar + 1 == stmt->domain->ncols);

    pluto_constraints_add_dim(stmt->domain, pos);
    stmt->dim++;
    stmt->iterators = (char **) realloc(stmt->iterators, stmt->dim*sizeof(char *));
    for (i=stmt->dim-2; i>=pos; i--) {
        stmt->iterators[i+1] = stmt->iterators[i];
    }
    stmt->iterators[pos] = strdup(iter);

    /* Stmt should always have a transformation */
    assert(stmt->trans != NULL);
    pluto_matrix_add_col(stmt->trans, pos);

    if (time_pos != -1) {
        pluto_matrix_add_row(stmt->trans, time_pos);
        stmt->trans->val[time_pos][pos] = 1;


        stmt->hyp_types = realloc(stmt->hyp_types, 
                sizeof(int)*stmt->trans->nrows);
        for (i=stmt->trans->nrows-2; i>=time_pos; i--) {
            stmt->hyp_types[i+1] = stmt->hyp_types[i];
        }
        stmt->hyp_types[time_pos] = hyp_type;
    }

    /* Update is_orig_loop */
    stmt->is_orig_loop = realloc(stmt->is_orig_loop, sizeof(bool)*stmt->dim);
    for (i=stmt->dim-2; i>=pos; i--) {
        stmt->is_orig_loop[i+1] = stmt->is_orig_loop[i];
    }
    stmt->is_orig_loop[pos] = true;

    for (i=0; i<stmt->nwrites; i++)   {
        pluto_matrix_add_col(stmt->writes[i]->mat, pos);
    }

    for (i=0; i<stmt->nreads; i++)   {
        pluto_matrix_add_col(stmt->reads[i]->mat, pos);
    }

    for (i=0; i<prog->ndeps; i++) {
        if (prog->deps[i]->src == stmt->id) {
            pluto_constraints_add_dim(prog->deps[i]->dpolytope, pos);
        }
        if (prog->deps[i]->dest == stmt->id) {
            pluto_constraints_add_dim(prog->deps[i]->dpolytope, 
                    prog->stmts[prog->deps[i]->src]->dim+pos);
        }
    }

    for (i=0; i<prog->ntransdeps; i++) {
        assert(prog->transdeps[i] != NULL);
        if (prog->transdeps[i]->src == stmt->id) {
            pluto_constraints_add_dim(prog->transdeps[i]->dpolytope, pos);
        }
        if (prog->transdeps[i]->dest == stmt->id) {
            pluto_constraints_add_dim(prog->transdeps[i]->dpolytope, 
                    prog->stmts[prog->transdeps[i]->src]->dim+pos);
        }
    }
}

/* Warning: use it only to knock off a dummy dimension (unrelated to 
 * anything else */
void pluto_stmt_remove_dim(Stmt *stmt, int pos, PlutoProg *prog)
{
    int i, npar;

    npar = stmt->domain->ncols - stmt->dim - 1;

    assert(pos <= stmt->dim);
    assert(stmt->dim + npar + 1 == stmt->domain->ncols);

    pluto_constraints_remove_dim(stmt->domain, pos);
    stmt->dim--;

    if (stmt->iterators != NULL) {
        free(stmt->iterators[pos]);
        for (i=pos; i<=stmt->dim-1; i++) {
            stmt->iterators[i] = stmt->iterators[i+1];
        }
        stmt->iterators = (char **) realloc(stmt->iterators, stmt->dim*sizeof(char *));
    }

    pluto_matrix_remove_col(stmt->trans, pos);

    /* Update is_orig_loop */
    for (i=pos; i<=stmt->dim-1; i++) {
        stmt->is_orig_loop[i] = stmt->is_orig_loop[i+1];
    }
    stmt->is_orig_loop = realloc(stmt->is_orig_loop, sizeof(bool)*stmt->dim);

    for (i=0; i<stmt->nwrites; i++)   {
        pluto_matrix_remove_col(stmt->writes[i]->mat, pos);
    }

    for (i=0; i<stmt->nreads; i++)   {
        pluto_matrix_remove_col(stmt->reads[i]->mat, pos);
    }

    for (i=0; i<prog->ndeps; i++) {
        if (prog->deps[i]->src == stmt->id) {
            pluto_constraints_remove_dim(prog->deps[i]->dpolytope, pos);
        }
        if (prog->deps[i]->dest == stmt->id) {
            // if (i==0)  printf("removing dim\n");
            pluto_constraints_remove_dim(prog->deps[i]->dpolytope, 
                    prog->stmts[prog->deps[i]->src]->dim+pos);
        }
    }

    for (i=0; i<prog->ntransdeps; i++) {
        assert(prog->transdeps[i] != NULL);
        if (prog->transdeps[i]->src == stmt->id) {
            pluto_constraints_remove_dim(prog->transdeps[i]->dpolytope, pos);
        }
        if (prog->transdeps[i]->dest == stmt->id) {
            // if (i==0)  printf("removing dim\n");
            pluto_constraints_remove_dim(prog->transdeps[i]->dpolytope,
                    prog->stmts[prog->transdeps[i]->src]->dim+pos);
        }
    }
}

void pluto_stmt_add_hyperplane(Stmt *stmt, PlutoHypType type, int pos)
{
    int i;

    assert(pos <= stmt->trans->nrows);

    pluto_matrix_add_row(stmt->trans, pos);

    stmt->hyp_types = realloc(stmt->hyp_types, 
            sizeof(int)*stmt->trans->nrows);
    for (i=stmt->trans->nrows-2; i>=pos; i--) {
        stmt->hyp_types[i+1] = stmt->hyp_types[i];
    }
    stmt->hyp_types[pos] = type;
}


void pluto_prog_add_hyperplane(PlutoProg *prog, int pos, PlutoHypType hyp_type)
{
    int i;

    prog->num_hyperplanes++;
    prog->hProps = (HyperplaneProperties *) realloc(prog->hProps, 
            prog->num_hyperplanes*sizeof(HyperplaneProperties));

    for (i=prog->num_hyperplanes-2; i>=pos; i--) {
        prog->hProps[i+1] = prog->hProps[i];
    }
    /* Initialize some */
    prog->hProps[pos].unroll = NO_UNROLL;
    prog->hProps[pos].prevec = 0;
    prog->hProps[pos].band_num = -1;
    prog->hProps[pos].dep_prop = UNKNOWN;
    prog->hProps[pos].type = hyp_type;
}


/* Statement that has the same transformed domain up to 'level' */
Stmt *create_helper_stmt(const Stmt *anchor_stmt, int level,
        const char *text, PlutoStmtType type)
{
    int i, npar;

    assert(level >= 0);
    assert(level <= anchor_stmt->trans->nrows);

    PlutoConstraints *newdom = pluto_get_new_domain(anchor_stmt);

    /* Lose everything but 0 to level-1 loops */

    pluto_constraints_project_out(newdom, level, 
            anchor_stmt->trans->nrows-level);

    npar = anchor_stmt->domain->ncols - anchor_stmt->dim - 1;
    PlutoMatrix *newtrans = pluto_matrix_alloc(level, newdom->ncols);

    /* Create new stmt */
    Stmt *newstmt = pluto_stmt_alloc(level, newdom, newtrans);

    newstmt->type = type;
    newstmt->parent_compute_stmt = (type == ORIG)? NULL: anchor_stmt;

    pluto_matrix_initialize(newstmt->trans, 0);
    for (i=0; i<newstmt->trans->nrows; i++)   {
        newstmt->trans->val[i][i] = 1;
    }
    newstmt->text = strdup(text);

    for (i=0; i<level; i++) {
        char *tmpstr = malloc(4);
        snprintf(tmpstr, 4, "t%d", i+1);
        newstmt->iterators[i] = tmpstr;
    }
    for (i=level; i<newstmt->dim;  i++) {
        char iter[5];
        sprintf(iter, "d%d", i-level+1);
        newstmt->iterators[i] = strdup(iter);
    }

    pluto_constraints_free(newdom);
    pluto_matrix_free(newtrans);

    assert(newstmt->dim+npar+1 == newstmt->domain->ncols);

    return newstmt;
}

/* Pad statement transformations so that they all equal number
 * of rows */
void pluto_pad_stmt_transformations(PlutoProg *prog)
{
    int max_nrows, i, j, nstmts;

    nstmts = prog->nstmts;
    Stmt **stmts = prog->stmts;

    /* Pad all trans if necessary with zeros */
    max_nrows = 0;
    for (i=0; i<nstmts; i++)    {
        if (stmts[i]->trans != NULL)    {
            max_nrows = PLMAX(max_nrows, stmts[i]->trans->nrows);
        }
    }

    if (max_nrows >= 1) {
        for (i=0; i<nstmts; i++)    {
            if (stmts[i]->trans == NULL)    {
                stmts[i]->trans = pluto_matrix_alloc(max_nrows, 
                        stmts[i]->dim+prog->npar+1);
                stmts[i]->trans->nrows = 0;
            }

            int curr_rows = stmts[i]->trans->nrows;

            /* Add all zero rows */
            for (j=curr_rows; j<max_nrows; j++)    {
                pluto_stmt_add_hyperplane(stmts[i], H_SCALAR, stmts[i]->trans->nrows);
            }
        }

        int old_hyp_num = prog->num_hyperplanes;
        for (i=old_hyp_num; i<max_nrows; i++) {
            /* This is not really H_SCALAR, but this is the best we can do */
            pluto_prog_add_hyperplane(prog, prog->num_hyperplanes, H_SCALAR);
        }
    }
}


/* Add statement to program; can't reuse arg stmt pointer any more */
void pluto_add_given_stmt(PlutoProg *prog, Stmt *stmt)
{
    prog->stmts = (Stmt **) realloc(prog->stmts, ((prog->nstmts+1)*sizeof(Stmt *)));

    stmt->id = prog->nstmts;

    prog->nvar = PLMAX(prog->nvar, stmt->dim);
    prog->stmts[prog->nstmts] = stmt;
    prog->nstmts++;

    pluto_pad_stmt_transformations(prog);

}



/* Create a statement and add it to the program
 * iterators: domain iterators
 * trans: schedule/transformation
 * domain: domain
 * text: statement text
 */
void pluto_add_stmt(PlutoProg *prog, 
        const PlutoConstraints *domain,
        const PlutoMatrix *trans,
        char ** iterators,
        const char *text,
        PlutoStmtType type)
{
    int i, nstmts;

    assert(trans != NULL);
    assert(trans->ncols == domain->ncols);

    nstmts = prog->nstmts;

    prog->stmts = (Stmt **) realloc(prog->stmts, ((nstmts+1)*sizeof(Stmt *)));

    Stmt *stmt = pluto_stmt_alloc(domain->ncols-prog->npar-1, domain, trans);

    stmt->id = nstmts;
    stmt->type = type;

    stmt->text = strdup(text);
    prog->nvar = PLMAX(prog->nvar, stmt->dim);

    for (i=0; i<stmt->dim; i++) {
        stmt->iterators[i] = strdup(iterators[i]);
    }

    prog->stmts[nstmts] = stmt;
    prog->nstmts++;

    pluto_pad_stmt_transformations(prog);
}


Dep *pluto_dep_alloc()
{
    Dep *dep = malloc(sizeof(Dep));

    dep->id = -1;
    dep->satvec = NULL;
    dep->depsat_poly = NULL;
    dep->satisfied = false;
    dep->satisfaction_level = -1;
    dep->dirvec = NULL;

    return dep;
}


Stmt *pluto_stmt_alloc(int dim, const PlutoConstraints *domain, 
        const PlutoMatrix *trans)
{
    int i;

    /* Have to provide a transformation */
    assert(trans != NULL);

    Stmt *stmt = (Stmt *) malloc(sizeof(Stmt));

    /* id will be assigned when added to PlutoProg */
    stmt->id = -1;
    stmt->dim = dim;
    stmt->dim_orig = dim;
    if (domain != NULL) {
        stmt->domain = pluto_constraints_dup(domain);
    }else{
        stmt->domain = NULL;
    }

    stmt->trans = pluto_matrix_dup(trans);

    stmt->hyp_types = malloc(stmt->trans->nrows*sizeof(int));
    for (i=0; i<stmt->trans->nrows; i++) {
        stmt->hyp_types[i] = H_LOOP;
    }

    stmt->text = NULL;
    stmt->tile =  1;
    stmt->num_tiled_loops = 0;
    stmt->reads = NULL;
    stmt->writes = NULL;
    stmt->nreads = 0;
    stmt->nwrites = 0;

    stmt->first_tile_dim = 0;
    stmt->last_tile_dim = -1;

    stmt->type = STMT_UNKNOWN;
    stmt->parent_compute_stmt = NULL;

    if (dim >= 1)   {
        stmt->is_orig_loop = (bool *) malloc(dim*sizeof(bool));
        stmt->iterators = (char **) malloc(sizeof(char *)*dim);
        for (i=0; i<stmt->dim; i++) {
            stmt->iterators[i] = NULL;
        }
    }else{
        stmt->is_orig_loop = NULL;
        stmt->iterators = NULL;
    }

    return stmt;
}


void pluto_access_free(PlutoAccess *acc)
{
    pluto_matrix_free(acc->mat);
    free(acc->name);
    scoplib_symbol_free(acc->symbol);
    free(acc);
}

void pluto_stmt_free(Stmt *stmt)
{
    int i, j;

    pluto_constraints_free(stmt->domain);

    pluto_matrix_free(stmt->trans);

    free(stmt->hyp_types);

    if (stmt->text != NULL) {
        free(stmt->text);
    }

    for (j=0; j<stmt->dim; j++)    {
        if (stmt->iterators[j] != NULL) {
            free(stmt->iterators[j]);
        }
    }

    /* If dim is zero, iterators, is_orig_loop are NULL */
    if (stmt->iterators != NULL)    {
        free(stmt->iterators);
        free(stmt->is_orig_loop);
    }

    PlutoAccess **writes = stmt->writes;
    PlutoAccess **reads = stmt->reads;

    if (writes != NULL) {
        for (i=0; i<stmt->nwrites; i++)   {
            pluto_access_free(writes[i]);
        }
        free(writes);
    }
    if (reads != NULL) {
        for (i=0; i<stmt->nreads; i++)   {
            pluto_access_free(reads[i]);
        }
        free(reads);
    }

    free(stmt);
}


/* Get transformed domain */
PlutoConstraints *pluto_get_new_domain(const Stmt *stmt)
{
    int i;
    PlutoConstraints *sched;

    PlutoConstraints *newdom = pluto_constraints_dup(stmt->domain);
    for (i=0; i<stmt->trans->nrows; i++)  {
        pluto_constraints_add_dim(newdom, 0);
    }

    sched = pluto_stmt_get_schedule(stmt);

    pluto_constraints_intersect(newdom, sched);

    // IF_DEBUG(printf("New pre-domain is \n"););
    // IF_DEBUG(pluto_constraints_print(stdout, newdom););

    pluto_constraints_project_out(newdom, 
            stmt->trans->nrows, stmt->dim);

    // IF_DEBUG(printf("New domain is \n"););
    // IF_DEBUG(pluto_constraints_print(stdout, newdom););

    pluto_constraints_free(sched);

    return newdom;
}


/* 
 * Checks if the range of the variable at depth 'depth' can be bound by a
 * constant; returns the constant of -1 if it can't be
 *
 * WARNING: If cnst is a list, looks at just the first element
 *
 * TODO: Not general now: difference being constant can be implied through
 * other inequalities 
 *
 * */
int get_const_bound_difference(const PlutoConstraints *cnst, int depth)
{
    int constdiff, r, r1, c, _lcm;

    assert(cnst != NULL);
    PlutoConstraints *cst = pluto_constraints_dup(cnst);

    pluto_constraints_project_out(cst, depth+1, cst->ncols-1-depth-1);
    assert(depth >= 0 && depth <= cst->ncols-2);

    // printf("Const bound diff at depth: %d\n", depth);
    // pluto_constraints_print(stdout, cst);

    constdiff = INT_MAX;

    for (r=0; r<cst->nrows; r++) {
        if (cst->val[r][depth] != 0)  break;
    }
    /* Variable doesn't appear */
    if (r==cst->nrows) return -1;

    /* Scale rows so that the coefficient of depth var is the same */
    _lcm = 1;
    for (r=0; r<cst->nrows; r++) {
        if (cst->val[r][depth] != 0) _lcm = lcm(_lcm, abs(cst->val[r][depth]));
    }
    for (r=0; r<cst->nrows; r++) {
        if (cst->val[r][depth] != 0) {
            for (c=0; c<cst->ncols; c++) {
                cst->val[r][c] = cst->val[r][c]*(_lcm/abs(cst->val[r][depth]));
            }
        }
    }

    /* Equality to a function of parameters/constant implies single point */
    for (r=0; r<cst->nrows; r++) {
        if (cst->is_eq[r] && cst->val[r][depth] != 0)  {
            for (c=depth+1; c<cst->ncols-1; c++)  { 
                if (cst->val[r][c] != 0)    {
                    break;
                }
            }
            if (c==cst->ncols-1) {
                constdiff = 1;
                //printf("constdiff is 1\n");
            }
        }
    }

    for (r=0; r<cst->nrows; r++) {
        if (cst->is_eq[r])  continue;
        if (cst->val[r][depth] <= -1)  {
            /* Find a lower bound with constant difference */
            for (r1=0; r1<cst->nrows; r1++) {
                if (cst->is_eq[r1])  continue;
                if (cst->val[r1][depth] >= 1) {
                    for (c=0; c<cst->ncols-1; c++)  { 
                        if (cst->val[r1][c] + cst->val[r][c] != 0)    {
                            break;
                        }
                    }
                    if (c==cst->ncols-1) {
                        constdiff = PLMIN(constdiff, 
                                floorf(cst->val[r][c]/(float)-cst->val[r][depth]) 
                                + ceilf(cst->val[r1][c]/(float)cst->val[r1][depth])
                                +1);
                    }
                }
            }
        }
    }
    pluto_constraints_free(cst);

    if (constdiff == INT_MAX)   {
        return -1;
    }

    /* Sometimes empty sets imply negative difference */
    /* It basically means zero points */
    if (constdiff <= -1) constdiff = 0;
    //printf("constdiff is %d\n", constdiff);

    return constdiff;
}

#define MINF 0
#define MAXF 1

/* Get expression for pos^{th} constraint in cst;
 * Returned string should be freed with 'free' */
char *get_expr(PlutoConstraints *cst, int pos, const char **params,
        int bound_type)
{
    int c, sum;

    char *expr = malloc(512);
    strcpy(expr, "");

    // printf("Get expr\n");
    // pluto_constraints_print(stdout, cst);

    if (bound_type == MINF) assert(cst->val[pos][0] <= -1);
    else assert(cst->val[pos][0] >= 1);

    sum = 0;
    for (c=1; c<cst->ncols-1; c++)    {
        sum += abs(cst->val[pos][c]);
    }

    if (sum == 0)   {
        /* constant */
        if (bound_type == MINF) {
            sprintf(expr+strlen(expr), "%d", 
                    (int)floorf(cst->val[pos][cst->ncols-1]/-(float)cst->val[pos][0]));
        }else{
            sprintf(expr+strlen(expr), "%d", 
                    (int)ceilf(-cst->val[pos][cst->ncols-1]/(float)cst->val[pos][0]));
        }
    }else{
        /* if it's being divided by 1, make it better by not putting
         * floor/ceil */
        if (abs(cst->val[pos][0]) != 1) {
            if (bound_type == MINF) {
                sprintf(expr+strlen(expr), "floorf((");
            }else{
                sprintf(expr+strlen(expr), "ceilf((");
            }
        }


        for (c=1; c<cst->ncols-1; c++)    {
            if (cst->val[pos][c] != 0) {
                if (bound_type == MINF) {
                    sprintf(expr+strlen(expr), (cst->val[pos][c] >= 1)? "+%d*%s": "%d*%s", 
                            cst->val[pos][c], params[c-1]);
                }else{
                    sprintf(expr+strlen(expr), (cst->val[pos][c] <= -1)? "+%d*%s": "%d*%s", 
                            -cst->val[pos][c], params[c-1]);
                }
            }
        }

        if (cst->val[pos][c] != 0) {
            if (bound_type == MINF) {
                sprintf(expr+strlen(expr), (cst->val[pos][c] >= 1)? "+%d": "%d", 
                        cst->val[pos][c]);
            }else{
                sprintf(expr+strlen(expr), (cst->val[pos][c] <= -1)? "+%d": "%d", 
                        -cst->val[pos][c]);
            }
        }

        /* if it's being divided by 1, make it better by not putting
         * floor/ceil */
        if (abs(cst->val[pos][0]) != 1) {
            sprintf(expr+strlen(expr), ")/(float)%d)",
                    (bound_type==MINF)? -cst->val[pos][0]: cst->val[pos][0]);
        }
    }

    return expr;
}

/*
 * Get min or max of all upper or lower bounds (resp).
 * Returned string should be freed with free
 */
char *get_func_of_expr(PlutoConstraints *cst, int offset, int bound_type,
        const char **params)
{
    char *fexpr;
    char *expr, *expr1;

    fexpr = malloc(512);

    strcpy(fexpr, "");

    char func[5];
    if (bound_type == MINF)  strcpy(func, "min(");
    else strcpy(func, "max(");

    if (cst->nrows - offset == 1) {
        expr = get_expr(cst, offset, params, bound_type);
        strcat(fexpr, expr);
    }else{
        /* cst->nrows >= 2 */
        expr = get_expr(cst, offset, params, bound_type);
        strcat(fexpr, func);
        strcat(fexpr, expr);
        expr1 = get_func_of_expr(cst, offset+1,bound_type,params);
        strcat(fexpr, ",");
        strcat(fexpr, expr1);
        strcat(fexpr, ")");
        free(expr1);
    }
    free(expr);

    return fexpr;
}

/* Return the size of the parametric bounding box for a (contiguous) 
 * block of dimensions
 * start: position of start of block
 * num: number of dimensions in block
 * npar: number of parameters in terms of which expression will be computed;
 * these are assumed to be the last 'npar' variables of cst
 * parmas: strings for 'npar' parameters
 * Return: expression describing the maximum number of points 'block' 
 * vars traverse for any value of '0..start-1' variables
 *
 * This function is constant-aware, i.e., if possible, it will exploit the
 * fact that the range of a variable is bounded by a constant. The underlying
 * call to get_parametric_extent_const for each of the 'num' dimensions
 * achieves this.
 */
char *get_parametric_bounding_box(const PlutoConstraints *cst, int start, 
        int num, int npar, const char **params)
{
    int k;
    char *buf_size;

    buf_size = malloc(2048 * 8);
    strcpy(buf_size, "(");

    const PlutoConstraints *cst_tmp = cst;
    while (cst_tmp != NULL) {
        sprintf(buf_size+strlen(buf_size), "+1");
        for (k=0; k<num; k++) {
            char *extent;
            get_parametric_extent_const(cst_tmp, start+k, npar,
                    params, &extent);
            sprintf(buf_size+strlen(buf_size), "*(%s)", extent);
            free(extent);
        }
        cst_tmp = cst_tmp->next;
    }
    sprintf(buf_size+strlen(buf_size), ")");

    return buf_size;
}


/*  Parametric extent of the pos^th variable in cst
 *  Extent computation is constant-aware, i.e., look when it can be 
 *  bounded by a constant; if not, just a difference of max and min 
 *  expressions of parameters is returned;  last 'npar'  ones are 
 *  treated as parameters; *extent should be freed by 'free' 
 */
void get_parametric_extent_const(const PlutoConstraints *cst, int pos,
        int npar, const char **params, char **extent)
{
    int constdiff;

    // printf("Parametric/const bounds at pos: %d\n", pos);
    //pluto_constraints_print(stdout, cst);

    constdiff = get_const_bound_difference(cst, pos);

    if (constdiff != -1)    {
        *extent = malloc(sizeof(int)*8);
        sprintf(*extent, "%d", constdiff);
    }else{
        get_parametric_extent(cst, pos, npar, params, extent);
    }
}


/* Get lower and upper bound expression as a function of parameters for pos^th
 * variable; last npar in cst are treated as parameters 
 * lbexpr and ubexpr should be freed with free
 * */
void get_lb_ub_expr(const PlutoConstraints *cst, int pos,
        int npar, const char **params, char **lbexpr, char **ubexpr)
{
    int i;
    PlutoConstraints *lb, *ub, *lbs, *ubs;
    char *lbe, *ube;

    PlutoConstraints *dup = pluto_constraints_dup(cst);

    pluto_constraints_project_out(dup, 0, pos);
    pluto_constraints_project_out(dup, 1, dup->ncols-npar-1-1);

    // printf("Parametric bounds at 0th pos\n");
    // pluto_constraints_print(stdout, dup);

    //pluto_constraints_simplify(dup);
    //pluto_constraints_print(stdout, dup);

    lbs = pluto_constraints_alloc(dup->nrows, dup->ncols);
    ubs = pluto_constraints_alloc(dup->nrows, dup->ncols);

    for (i=0; i<dup->nrows; i++)    {
        if (dup->is_eq[i] && dup->val[i][0] != 0) {
            lb = pluto_constraints_select_row(dup, i);
            pluto_constraints_add(lbs, lb);
            pluto_constraints_free(lb);

            ub = pluto_constraints_select_row(dup, i);
            pluto_constraints_negate_row(ub, 0);
            pluto_constraints_add(ubs, ub);
            pluto_constraints_free(ub);
        }
        if (dup->val[i][0] >= 1)    {
            /* Lower bound */
            lb = pluto_constraints_select_row(dup, i);
            pluto_constraints_add(lbs, lb);
            pluto_constraints_free(lb);
        }else if (dup->val[i][0] <= -1) {
            /* Upper bound */
            ub = pluto_constraints_select_row(dup, i);
            pluto_constraints_add(ubs, ub);
            pluto_constraints_free(ub);
        }
    }

    assert(lbs->nrows >= 1);
    assert(ubs->nrows >= 1);
    pluto_constraints_free(dup);

    lbe = get_func_of_expr(lbs, 0, MAXF, params);
    ube = get_func_of_expr(ubs, 0, MINF, params);

    *lbexpr = lbe;
    *ubexpr = ube;

    // printf("lbexpr: %s\n", lbe);
    // printf("ubexpr: %s\n", ube);

    pluto_constraints_free(lbs);
    pluto_constraints_free(ubs);
}


/* 
 * Get expression for difference of upper and lower bound of pos^th variable
 * in cst in terms of parameters;  last 'npar' dimensions of cst are treated 
 * as parameters; *extent should be freed by 'free'
 */
void get_parametric_extent(const PlutoConstraints *cst, int pos,
        int npar, const char **params, char **extent)
{
    char *lbexpr, *ubexpr;

    get_lb_ub_expr(cst, pos, npar, params, &lbexpr, &ubexpr);

    if (!strcmp(lbexpr, ubexpr)) {
        *extent = strdup("1");
    }else{
        *extent = malloc(strlen(lbexpr) + strlen(ubexpr) + strlen(" -  + 1")+1);
        sprintf(*extent, "%s - %s + 1", ubexpr, lbexpr);
    }

#if 0
    if (cst->next != NULL)  {
        char *extent_next;
        get_parametric_extent(cst->next, pos, npar, params, &extent_next);
        *extent = realloc(*extent, strlen(*extent)+strlen(extent_next) + strlen(" + "));
        sprintf(*extent+strlen(*extent), " + %s", extent_next);
        free(extent_next);
    }
#endif

    // printf("Extent: %s\n", *extent);

    free(lbexpr);
    free(ubexpr);
}


char *get_data_extent(PlutoAccess *acc, char **params, int npars, int dim)
{
    return scoplib_symbol_table_get_bound(acc->symbol, dim, params, npars);
}

/* Get Alpha matrix (A matrix - INRIA transformation representation */
PlutoMatrix *get_alpha(const Stmt *stmt, const PlutoProg *prog)
{
    int r, c, i;

    PlutoMatrix *a;
    a = pluto_matrix_alloc(stmt->dim, stmt->dim);

    r=0;
    for (i=0; i<stmt->trans->nrows; i++)    {
        if (stmt->hyp_types[i] == H_LOOP || 
                stmt->hyp_types[i] == H_TILE_SPACE_LOOP) {
            for (c=0; c<stmt->dim; c++) {
                a->val[r][c] = stmt->trans->val[i][c];
            }
            r++;
            if (r==stmt->dim)   break;
        }
    }

    assert(r==stmt->dim);

    return a;
}


int pluto_is_hyperplane_scalar(const Stmt *stmt, int level)
{
    int j;

    assert(level <= stmt->trans->nrows-1);

    for (j=0; j<stmt->dim; j++) {
        if (stmt->trans->val[level][j] != 0) return 0;
    }

    return 1;
}


int pluto_is_hyperplane_loop(const Stmt *stmt, int level)
{
    return !pluto_is_hyperplane_scalar(stmt, level);
}

/* Get the remapping matrix: maps time iterators back to the domain 
 * iterators; divs: divisors for the rows */
PlutoMatrix *pluto_stmt_get_remapping(const Stmt *stmt, int **divs)
{
    int i, j, k, _lcm, factor1, npar;

    PlutoMatrix *remap, *trans;

    trans = stmt->trans;
    remap = pluto_matrix_dup(trans);

    npar = stmt->domain->ncols - stmt->dim - 1;

    *divs = malloc(sizeof(int)*(stmt->dim+npar+1));

    for (i=0; i<remap->nrows; i++)  {
        pluto_matrix_negate_row(remap, remap->nrows-1-i);
        pluto_matrix_add_col(remap, 0);
        remap->val[trans->nrows-1-i][0] = 1;
    }

    /* Bring the stmt iterators to the left */
    for (i=0; i<stmt->dim; i++)  {
        pluto_matrix_move_col(remap, remap->nrows+i, i);
    }

    assert(stmt->dim <= remap->nrows);

    for (i=0; i<stmt->dim; i++)  {
        // pluto_matrix_print(stdout, remap);
        if (remap->val[i][i] == 0) {
            for (k=i+1; k<remap->nrows; k++) {
                if (remap->val[k][i] != 0) break;
            }
            if (k<remap->nrows)    {
                pluto_matrix_interchange_rows(remap, i, k);
            }else{
                /* Can't associate domain iterator with time iterator */
                /* Shouldn't happen with a full-ranked transformation */
                printf("Can't associate domain iterator #%d with time iterators\n", i+1);
                pluto_matrix_print(stdout, remap);
                assert(0);
            }
        }
        //printf("after interchange %d\n", i); 
        //pluto_matrix_print(stdout, remap);
        assert(remap->val[i][i] != 0);
        for (k=i+1; k<remap->nrows; k++) {
            if (remap->val[k][i] == 0) continue;
            _lcm = lcm(remap->val[k][i], remap->val[i][i]);
            factor1 = _lcm/remap->val[k][i];
            for (j=i; j<remap->ncols; j++) {
                remap->val[k][j] = remap->val[k][j]*factor1
                    - remap->val[i][j]*(_lcm/remap->val[i][i]);
            }

        }
        //printf("after iteration %d\n", i); 
        //pluto_matrix_print(stdout, remap);
    }

    //pluto_matrix_print(stdout, remap);

    /* Solve upper triangular system now */
    for (i=stmt->dim-1; i>=0; i--)  {
        assert(remap->val[i][i] != 0);
        for (k=i-1; k>=0; k--) {
            if (remap->val[k][i] == 0) continue;
            _lcm = lcm(remap->val[k][i], remap->val[i][i]);
            factor1 = _lcm/remap->val[k][i];
            for (j=0; j<remap->ncols; j++) {
                remap->val[k][j] = remap->val[k][j]*(factor1) 
                    - remap->val[i][j]*(_lcm/remap->val[i][i]);
            }
        }
    }

    assert(remap->nrows >= stmt->dim);
    for (i=remap->nrows-1; i>=stmt->dim; i--) {
        pluto_matrix_remove_row(remap, remap->nrows-1);
    }
    // pluto_matrix_print(stdout, remap);

    for (i=0; i<stmt->dim; i++) {
        assert(remap->val[i][i] != 0);
        if (remap->val[i][i] <= -1) {
            pluto_matrix_negate_row(remap, i);
        }
        (*divs)[i] = abs(remap->val[i][i]);
    }
    // pluto_matrix_print(stdout, remap);

    for (i=0; i<stmt->dim; i++) {
        pluto_matrix_remove_col(remap, 0);
    }

    for (i=0; i<stmt->dim; i++) {
        pluto_matrix_negate_row(remap, i);
    }

    /* Identity for the parameter and constant part */
    for (i=0; i<npar+1; i++) {
        pluto_matrix_add_row(remap, remap->nrows);
        remap->val[remap->nrows-1][remap->ncols-npar-1+i] = 1;
        (*divs)[stmt->dim+i] = 1;
    }

    // printf("Remapping using new technique is\n");
    // pluto_matrix_print(stdout, remap);

    return remap;
}


void pluto_prog_params_print(const PlutoProg *prog)
{
    int i;
    for (i=0; i<prog->npar; i++) {
        printf("%s\n", prog->params[i]);
    }
}


/* Get new access function */
PlutoMatrix *pluto_get_new_access_func(const Stmt *stmt, 
        const PlutoMatrix *acc, int **divs) 
{
    PlutoMatrix *remap, *newacc;
    int r, c, npar, *remap_divs;

    npar = stmt->domain->ncols - stmt->dim - 1;
    *divs = malloc(sizeof(int)*acc->nrows);

    // printf("Old access function is \n");;
    // pluto_matrix_print(stdout, acc);;

    // printf("Stmt trans\n");
    // pluto_matrix_print(stdout, stmt->trans);

    remap = pluto_stmt_get_remapping(stmt, &remap_divs);
    // printf("Remapping matrix\n");
    // pluto_matrix_print(stdout, remap);
    //

    int _lcm = 1;
    for (r=0; r<remap->nrows; r++) {
        assert(remap_divs[r] != 0);
        _lcm = lcm(_lcm,remap_divs[r]);
    }
    for (r=0; r<remap->nrows; r++) {
        for (c=0; c<remap->ncols; c++) {
            remap->val[r][c] = (remap->val[r][c]*_lcm)/remap_divs[r];
        }
    }

    newacc = pluto_matrix_product(acc, remap);

    for (r=0; r<newacc->nrows; r++) {
        (*divs)[r] = _lcm;
    }

    // printf("New access function is \n");
    // pluto_matrix_print(stdout, newacc);

    assert(newacc->ncols = stmt->trans->nrows+npar+1);

    pluto_matrix_free(remap);
    free(remap_divs);

    return newacc;
}


/* Separates a list of statements at level 'level' */
void pluto_separate_stmts(PlutoProg *prog, Stmt **stmts, int num, int level)
{
    int i, nstmts, k;

    nstmts = prog->nstmts;

    // pluto_matrix_print(stdout, stmt->trans);
    for (i=0; i<nstmts; i++)    {
        pluto_stmt_add_hyperplane(prog->stmts[i], H_SCALAR, level);
    }
    // pluto_matrix_print(stdout, stmt->trans);
    for (k=0; k<num; k++)   {
        stmts[k]->trans->val[level][stmts[k]->trans->ncols-1] = 1+k;
    }

    pluto_prog_add_hyperplane(prog, level, H_SCALAR);
    prog->hProps[level].dep_prop = SEQ;
}


/* Separates a statement from the rest (places it later) at level 'level';
 * this is done by inserting a scalar dimension separating them */
void pluto_separate_stmt(PlutoProg *prog, const Stmt *stmt, int level)
{
    int i, nstmts;

    nstmts = prog->nstmts;

    // pluto_matrix_print(stdout, stmt->trans);
    for (i=0; i<nstmts; i++)    {
        pluto_stmt_add_hyperplane(prog->stmts[i], H_SCALAR, level);
    }
    // pluto_matrix_print(stdout, stmt->trans);
    stmt->trans->val[level][stmt->trans->ncols-1] = 1;

    pluto_prog_add_hyperplane(prog, level, H_SCALAR);
    prog->hProps[level].dep_prop = SEQ;
}

int pluto_stmt_is_member_of(const Stmt *s, Stmt **slist, int len)
{
    int i;
    for (i=0; i<len; i++) {
        if (s->id == slist[i]->id) return 1;
    }
    return 0;
}


int pluto_stmt_is_subset_of(Stmt **s1, int n1, Stmt **s2, int n2)
{
    int i;

    for (i=0; i<n1; i++) {
        if (!pluto_stmt_is_member_of(s1[i], s2, n2)) return 0;
    }

    return 1;
}

void add_if_new(PlutoAccess ***accs, int *num, PlutoAccess *new)
{
    int i;

    for (i=0; i<*num; i++) {
        if (!strcmp((*accs)[i]->name, new->name)) {
            break;
        }
    }

    if (i==*num) {
        *accs = realloc(*accs, (*num+1)*sizeof(PlutoAccess *));
        (*accs)[*num] = new;
        (*num)++;
    }
}


/* Get all write accesses in the program */
PlutoAccess **pluto_get_all_waccs(PlutoProg *prog, int *num)
{
    int i;

    PlutoAccess **accs = NULL;
    *num = 0;

    for (i=0; i<prog->nstmts; i++) {
        assert(prog->stmts[i]->nwrites == 1);
        add_if_new(&accs, num, prog->stmts[i]->writes[0]);
    }
    return accs;
}

/* Temporary data structure used inside extra_stmt_domains
 *
 * stmts points to the array of Stmts being constructed
 * index is the index of the next stmt in the array
 */
struct pluto_extra_stmt_info {
    Stmt **stmts;
    int index;
};

static int extract_basic_set(__isl_take isl_basic_set *bset, void *user)
{
    Stmt **stmts;
    Stmt *stmt;
    PlutoConstraints *bcst;
    struct pluto_extra_stmt_info *info;

    info = (struct pluto_extra_stmt_info *)user;

    stmts = info->stmts;
    stmt = stmts[info->index];

    bcst = isl_basic_set_to_pluto_constraints(bset);
    if (stmt->domain) {
        stmt->domain = pluto_constraints_unionize_simple(stmt->domain, bcst);
        pluto_constraints_free(bcst);
    }else{
        stmt->domain = bcst;
    }

    isl_basic_set_free(bset);
    return 0;
}

static int extract_stmt(__isl_take isl_set *set, void *user)
{
    int r;
    Stmt **stmts;
    int id;

    stmts = (Stmt **) user;

    int dim = isl_set_dim(set, isl_dim_all);
    int npar = isl_set_dim(set, isl_dim_param);
    PlutoMatrix *trans = pluto_matrix_alloc(dim-npar, dim+1);
    pluto_matrix_initialize(trans, 0);
    trans->nrows = 0;

    id = atoi(isl_set_get_tuple_name(set)+2);

    stmts[id] = pluto_stmt_alloc(dim-npar, NULL, trans);

    Stmt *stmt = stmts[id];
    stmt->type = ORIG;
    stmt->id = id;

    struct pluto_extra_stmt_info info = {stmts, id};
    r = isl_set_foreach_basic_set(set, &extract_basic_set, &info);

    pluto_matrix_free(trans);

    int j;
    for (j=0; j<stmt->dim; j++)  {
        stmt->is_orig_loop[j] = true;
    }

    isl_set_free(set);

    return r;
}

int extract_stmts(__isl_keep isl_union_set *domains, Stmt **stmts)
{
    isl_union_set_foreach_set(domains, &extract_stmt, stmts);

    return 0;
}

int pluto_get_max_ind_hyps_non_scalar(const PlutoProg *prog)
{
    int max, i;

    max = 0;

    for (i=0; i<prog->nstmts; i++) {
        max = PLMAX(max, pluto_stmt_get_num_ind_hyps_non_scalar(prog->stmts[i]));
    }

    return max;
}

int pluto_get_max_ind_hyps(const PlutoProg *prog)
{
    int max, i;

    max = 0;

    for (i=0; i<prog->nstmts; i++) {
        max = PLMAX(max, pluto_stmt_get_num_ind_hyps(prog->stmts[i]));
    }

    return max;
}

int pluto_stmt_get_num_ind_hyps_non_scalar(const Stmt *stmt)
{
    int isols, i,j=0;

    PlutoMatrix *tprime = pluto_matrix_dup(stmt->trans);

    /* Ignore padding dimensions, params, and constant part */
    for (i=stmt->dim_orig; i<stmt->trans->ncols; i++) {
        pluto_matrix_remove_col(tprime, stmt->dim_orig);
    }
    for (i=0; i<stmt->trans->nrows; i++) {
        if (stmt->hyp_types[i]==H_SCALAR) {   
            pluto_matrix_remove_row(tprime, i-j); 
            j++; 
        }
    }

    isols = pluto_matrix_get_rank(tprime);
    pluto_matrix_free(tprime);

    return isols;
}

int pluto_stmt_get_num_ind_hyps(const Stmt *stmt)
{
    int isols, i;

    PlutoMatrix *tprime = pluto_matrix_dup(stmt->trans);

    /* Ignore padding dimensions, params, and constant part */
    for (i=stmt->dim_orig; i<stmt->trans->ncols; i++) {
        pluto_matrix_remove_col(tprime, stmt->dim_orig);
    }

    isols = pluto_matrix_get_rank(tprime);
    pluto_matrix_free(tprime);

    return isols;
}

int pluto_transformations_full_ranked(PlutoProg *prog)
{
    int i;

    for (i=0; i<prog->nstmts; i++) {
        if (pluto_stmt_get_num_ind_hyps(prog->stmts[i]) < prog->stmts[i]->dim_orig) {
            return 0;
        }
    }

    return 1;
}
