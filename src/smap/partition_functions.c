/*****************************************************************************\
 *  partition_functions.c - Functions related to partition display 
 *  mode of smap.
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#include <stdlib.h>

#include "config.h"
#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/xmalloc.h"
#include "src/smap/smap.h"

#define _DEBUG 0
DEF_TIMERS;

typedef struct {
	char *bgl_user_name;
	char *bgl_block_name;
	char *slurm_part_name;
	char *nodes;
	enum connection_type bgl_conn_type;
	enum node_use_type bgl_node_use;
	rm_partition_state_t state;
	hostlist_t hostlist;
	bool printed;

} db2_block_info_t;

static List block_list = NULL;

static char* _convert_conn_type(enum connection_type conn_type);
static char* _convert_node_use(enum node_use_type node_use);
static void _print_header_part(void);
static char *_part_state_str(rm_partition_state_t state);
static int  _print_text_part(partition_info_t * part_ptr, 
			     db2_block_info_t *db2_info_ptr);
static void _read_part_db2(void);
static int _in_slurm_partition(char *nodes, int *first, int *last);
static int _print_rest(void *object, void *arg);

void get_slurm_part(void)
{
	int error_code, i, j, recs, count = 0;
	static partition_info_msg_t *part_info_ptr = NULL, *new_part_ptr;
	partition_info_t part;

	if (part_info_ptr) {
		error_code = slurm_load_partitions(part_info_ptr->last_update, 
						   &new_part_ptr, 0);
		if (error_code == SLURM_SUCCESS)
			slurm_free_partition_info_msg(part_info_ptr);
		else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_SUCCESS;
			new_part_ptr = part_info_ptr;
		}
	} else {
		error_code = slurm_load_partitions((time_t) NULL, 
						   &new_part_ptr, 0);
	}
	if (error_code) {
		if (quiet_flag != 1) {
			mvwprintw(pa_system_ptr->text_win,
				  pa_system_ptr->ycord, 1,
				  "slurm_load_partitions: %s",
				  slurm_strerror(slurm_get_errno()));
			pa_system_ptr->ycord++;
		}
	}

	if (!params.no_header)
		_print_header_part();

	if (new_part_ptr)
		recs = new_part_ptr->record_count;
	else
		recs = 0;

	for (i = 0; i < recs; i++) {
		j = 0;
		part = new_part_ptr->partition_array[i];
		
		if (!part.nodes || (part.nodes[0] == '\0'))
			continue;	/* empty partition */
		
				
		while (part.node_inx[j] >= 0) {
			set_grid(part.node_inx[j],
				 part.node_inx[j + 1], count);
			j += 2;
		}
		part.root_only =
			(int) pa_system_ptr->
			fill_in_value[count].letter;
		wattron(pa_system_ptr->text_win,
			COLOR_PAIR(pa_system_ptr->
				   fill_in_value[count].
				   color));
		_print_text_part(&part, NULL);
		wattroff(pa_system_ptr->text_win,
			 COLOR_PAIR(pa_system_ptr->
				    fill_in_value[count].
				    color));
		count++;
			
	}
	
	part_info_ptr = new_part_ptr;
	return;
}
/*
 * _diff_tv_str - build a string showing the time difference between two times
 * IN tv1 - start of event
 * IN tv2 - end of event
 * OUT tv_str - place to put delta time in format "usec=%ld"
 * IN len_tv_str - size of tv_str in bytes
 */
static void diff_tv_str(struct timeval *tv1,struct timeval *tv2,
		char *tv_str, int len_tv_str)
{
	long delta_t;
	delta_t  = (tv2->tv_sec  - tv1->tv_sec) * 1000000;
	delta_t +=  tv2->tv_usec - tv1->tv_usec;
	snprintf(tv_str, len_tv_str, "usec=%ld", delta_t);
}

void get_bgl_part(void)
{
	int error_code, i, j, recs=0, count = 0;
	static partition_info_msg_t *part_info_ptr = NULL, *new_part_ptr;
	partition_info_t part;
	//char node_entry[13];
	int number, start[PA_SYSTEM_DIMENSIONS], end[PA_SYSTEM_DIMENSIONS];
	//db2_block_info_t *block_ptr;
	//bool lower;
	db2_block_info_t *db2_info_ptr;
	ListIterator itr;
	
	if (part_info_ptr) {
		error_code = slurm_load_partitions(part_info_ptr->last_update, 
						   &new_part_ptr, 0);
		if (error_code == SLURM_SUCCESS)
			slurm_free_partition_info_msg(part_info_ptr);
		else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_SUCCESS;
			new_part_ptr = part_info_ptr;
		}
	} else {
		error_code = slurm_load_partitions((time_t) NULL, 
						   &new_part_ptr, 0);
	}

	if (error_code) {
		if (quiet_flag != 1) {
			mvwprintw(pa_system_ptr->text_win,
				  pa_system_ptr->ycord, 1,
				  "slurm_load_partitions: %s",
				  slurm_strerror(slurm_get_errno()));
			pa_system_ptr->ycord++;
		}
	}
	
	
	_read_part_db2();
	

	if (!params.no_header)
		_print_header_part();

	if (new_part_ptr)
		recs = new_part_ptr->record_count;
	else
		recs = 0;
	for (i = 0; i < recs; i++) {
		j = 0;
		part = new_part_ptr->partition_array[i];
		
		if (!part.nodes || (part.nodes[0] == '\0'))
			continue;	/* empty partition */
		while (part.nodes[j] != '\0') {
			if ((part.nodes[j]   == '[')
			    && (part.nodes[j+8] == ']')
			    && ((part.nodes[j+4] == 'x')
				|| (part.nodes[j+4] == '-'))) {
				j++;
				number = atoi(part.nodes + j);
				start[X] = number / 100;
				start[Y] = (number % 100) / 10;
				start[Z] = (number % 10);
				j += 4;

				number = atoi(part.nodes + j);
				end[X] = number / 100;
				end[Y] = (number % 100) / 10;
				end[Z] = (number % 10);
				break;
			}
			j++;
		}
		
		if (block_list) {
			itr = list_iterator_create(block_list);
			while ((db2_info_ptr = (db2_block_info_t*) list_next(itr)) != NULL)
				if(_in_slurm_partition(db2_info_ptr->nodes, start, end))
					db2_info_ptr->slurm_part_name = xstrdup(part.name);
			list_iterator_destroy(itr);
		}
	}

	/* Report any BGL Blocks not in a SLURM partition */
	if (block_list) {
		list_for_each(block_list, _print_rest, &count);
	}

	part_info_ptr = new_part_ptr;
	return;
}

static void _print_header_part(void)
{
	mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
		  pa_system_ptr->xcord, "ID");
	pa_system_ptr->xcord += 4;
	mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
		  pa_system_ptr->xcord, "PARTITION");
	pa_system_ptr->xcord += 10;
	
	if (params.display != BGLPART) {
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "AVAIL");
		pa_system_ptr->xcord += 7;
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "TIMELIMIT");
		pa_system_ptr->xcord += 11;
	} else {
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "BGL_BLOCK");
		pa_system_ptr->xcord += 12;
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "STATE");
		pa_system_ptr->xcord += 8;
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "USER");
		pa_system_ptr->xcord += 12;
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "CONN");
		pa_system_ptr->xcord += 6;
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "NODE_USE");
		pa_system_ptr->xcord += 10;
	}

	mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
		  pa_system_ptr->xcord, "NODES");
	pa_system_ptr->xcord += 7;
	mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
		  pa_system_ptr->xcord, "NODELIST");
	pa_system_ptr->xcord = 1;
	pa_system_ptr->ycord++;
}

static char *_part_state_str(rm_partition_state_t state)
{
	static char tmp[16];

#ifdef HAVE_BGL_FILES
	switch (state) {
		case RM_PARTITION_BUSY: 
			return "BUSY";
		case RM_PARTITION_CONFIGURING:
			return "CONFIG";
		case RM_PARTITION_DEALLOCATING:
			return "DEALLOC";
		case RM_PARTITION_ERROR:
			return "ERROR";
		case RM_PARTITION_FREE:
			return "FREE";
		case RM_PARTITION_NAV:
			return "NAV";
		case RM_PARTITION_READY:
			return "READY";
	}
#endif

	snprintf(tmp, sizeof(tmp), "%d", state);
	return tmp;
}

static int _print_text_part(partition_info_t * part_ptr, 
			    db2_block_info_t *db2_info_ptr)
{
	int printed = 0;
	int tempxcord;
	int prefixlen;
	int i = 0;
	int width = 0;
	char *nodes, time_buf[20];

	mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
		  pa_system_ptr->xcord, "%c", part_ptr->root_only);
	pa_system_ptr->xcord += 4;

	if (part_ptr->name) {
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "%.9s", part_ptr->name);
		pa_system_ptr->xcord += 10;
		if (params.display != BGLPART) {
			if (part_ptr->state_up)
				mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
					  pa_system_ptr->xcord, "UP");
			else
				mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
					  pa_system_ptr->xcord, "DOWN");
			pa_system_ptr->xcord += 7;
			
			if (part_ptr->max_time == INFINITE)
				snprintf(time_buf, sizeof(time_buf), "UNLIMITED");
			else {
				snprint_time(time_buf, sizeof(time_buf), 
					     (part_ptr->max_time * 60));
			}
			
			width = strlen(time_buf);
			mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
				  pa_system_ptr->xcord + (9 - width), "%s", 
				  time_buf);
			pa_system_ptr->xcord += 11;
		} 
	} else
		pa_system_ptr->xcord += 10;

	if (params.display == BGLPART) {
		if (db2_info_ptr) {
			mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
				  pa_system_ptr->xcord, "%.11s", 
				  db2_info_ptr->bgl_block_name);
			pa_system_ptr->xcord += 12;
			mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
				  pa_system_ptr->xcord, _part_state_str(db2_info_ptr->state));
				pa_system_ptr->xcord += 8;
				
			mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
				  pa_system_ptr->xcord, "%.11s", 
				  db2_info_ptr->bgl_user_name);
			pa_system_ptr->xcord += 12;
			
			mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
				  pa_system_ptr->xcord, "%.5s", 
				  _convert_conn_type(
					  db2_info_ptr->bgl_conn_type));
			pa_system_ptr->xcord += 6;
			mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
				  pa_system_ptr->xcord, "%.9s",
				  _convert_node_use(
					  db2_info_ptr->bgl_node_use));
			pa_system_ptr->xcord += 10;
		} else {
			mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
				  pa_system_ptr->xcord, "?");
			pa_system_ptr->xcord += 12;
			mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
				  pa_system_ptr->xcord, "?");
			pa_system_ptr->xcord += 8;
			mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
				  pa_system_ptr->xcord, "?");
			pa_system_ptr->xcord += 12;
			mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
				  pa_system_ptr->xcord, "?");
			pa_system_ptr->xcord += 6;
			mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
				  pa_system_ptr->xcord, "?");
			pa_system_ptr->xcord += 10;
		}
	}

	mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
		  pa_system_ptr->xcord, "%5d", part_ptr->total_nodes);
	pa_system_ptr->xcord += 7;

	tempxcord = pa_system_ptr->xcord;
	//width = pa_system_ptr->text_win->_maxx - pa_system_ptr->xcord;
	if (params.display == BGLPART)
		nodes = part_ptr->allow_groups;
	else
		nodes = part_ptr->nodes;
	prefixlen = i;
	while (nodes[i] != '\0') {
		width = pa_system_ptr->text_win->_maxx - pa_system_ptr->xcord;

		if (!prefixlen && nodes[i] == '[' && nodes[i - 1] == ',')
			prefixlen = i + 1;

		if (nodes[i - 1] == ',' && (width - 12) <= 0) {
			pa_system_ptr->ycord++;
			pa_system_ptr->xcord = tempxcord + prefixlen;
		} else if (pa_system_ptr->xcord >
			   pa_system_ptr->text_win->_maxx) {
			pa_system_ptr->ycord++;
			pa_system_ptr->xcord = tempxcord + prefixlen;
		}


		if ((printed = mvwaddch(pa_system_ptr->text_win,
					pa_system_ptr->ycord, pa_system_ptr->xcord,
					nodes[i])) < 0)
			return printed;
		pa_system_ptr->xcord++;

		i++;
	}

	pa_system_ptr->xcord = 1;
	pa_system_ptr->ycord++;

	return printed;
}

#ifdef HAVE_BGL_FILES
static void _block_list_del(void *object)
{
	db2_block_info_t *block_ptr;

	if (block_ptr) {
		xfree(block_ptr->bgl_user_name);
		xfree(block_ptr->bgl_block_name);
		xfree(block_ptr->slurm_part_name);
		xfree(block_ptr->nodes);
		if (block_ptr->hostlist)
			hostlist_destroy(block_ptr->hostlist);
		
		xfree(block_ptr);
		
	}
}

/*
 * Convert a BGL API error code to a string
 * IN inx - error code from any of the BGL Bridge APIs
 * RET - string describing the error condition
 */
char *bgl_err_str(status_t inx)
{
	switch (inx) {
	case STATUS_OK:
		return "Status OK";
	case PARTITION_NOT_FOUND:
		return "Partition not found";
	case JOB_NOT_FOUND:
		return "Job not found";
	case BP_NOT_FOUND:
		return "Base partition not found";
	case SWITCH_NOT_FOUND:
		return "Switch not found";
	case JOB_ALREADY_DEFINED:
		return "Job already defined";
	case CONNECTION_ERROR:
		return "Connection error";
	case INTERNAL_ERROR:
		return "Internal error";
	case INVALID_INPUT:
		return "Invalid input";
	case INCOMPATIBLE_STATE:
		return "Incompatible state";
	case INCONSISTENT_DATA:
		return "Inconsistent data";
	}

	return "?";
}

static int _part_list_find(void *object, void *key)
{
	db2_block_info_t *block_ptr = (db2_block_info_t *) object;
	char *block_name = (char *) key;

	if (!block_ptr || (block_ptr->bgl_block_name == NULL)) {
		fprintf(stderr, "_part_list_find: block_ptr == NULL\n");
		return -1;
	}
	if (!block_name) {
		fprintf(stderr, "_part_list_find: key == NULL\n");
		return -1;
	}

        if (strcmp(block_ptr->bgl_block_name, block_name) == 0)
                return 1;
        return 0;
}

static int _list_match_all(void *object, void *key)
{
	return 1;
}

static int _clear_printed_flag(void *object, void *arg)
{
	db2_block_info_t *block_ptr = (db2_block_info_t *) object;
	block_ptr->printed = false;
	return SLURM_SUCCESS;
}
#endif

static int _in_slurm_partition(char *nodes, int *first, int *last)
{
	int number;
	int start[PA_SYSTEM_DIMENSIONS], end[PA_SYSTEM_DIMENSIONS];
	int j=0;
	
	while (nodes[j] != '\0') {
		if ((nodes[j]   == '[')
		    && (nodes[j+8] == ']')
		    && ((nodes[j+4] == 'x')
			|| (nodes[j+4] == '-'))) {
			j++;
			number = atoi(nodes + j);
			start[X] = number / 100;
			start[Y] = (number % 100) / 10;
			start[Z] = (number % 10);
			j += 4;
			number = atoi(nodes + j);
			end[X] = number / 100;
			end[Y] = (number % 100) / 10;
			end[Z] = (number % 10);
			j += 5;
			if(nodes[j] != ',')
				break;
		} else if((nodes[j] < 58 && nodes[j] > 47) 
			  && nodes[j-1] != '[') {
			number = atoi(nodes + j);
			start[X] = end[X] = number / 100;
			start[Y] = end[Y] = (number % 100) / 10;
			start[Z] = end[Z] = (number % 10);
			j+=3;
			if(nodes[j] != ',')
				break;	
		}
				
		j++;
	}
	if((start[X]>=first[X])
	   && (start[Y]>=first[Y])
	   && (start[Z]>=first[Z])
	   && (end[X]<=last[X])
	   && (end[Y]<=last[Y])
	   && (end[Z]<=last[Z]))
		return 1;
	else 
		return 0;
	
}

static int _print_rest(void *object, void *arg)
{
	db2_block_info_t *block_ptr = (db2_block_info_t *) object;
	int *count = (int *) arg;
	int number;
	int start[PA_SYSTEM_DIMENSIONS], end[PA_SYSTEM_DIMENSIONS];
	partition_info_t part;

	if (block_ptr->printed)
		return SLURM_SUCCESS;

	part.total_nodes = 0;
		
	if (block_ptr->nodes[11] == ']') {	/* "bgl[###x###]" */
		number = atoi(block_ptr->nodes + 4);
		start[X] = number / 100;
		start[Y] = (number % 100) / 10;
		start[Z] = (number % 10);
		
		number = atoi(block_ptr->nodes + 8);
		end[X] = number / 100;
		end[Y] = (number % 100) / 10;
		end[Z] = (number % 10);
		
		part.total_nodes += set_grid_bgl(start, end, *count);
	} else {				/* any other format */
		hostlist_t hostlist;
		hostlist_iterator_t host_iter;
		char *host_name;

		hostlist  = hostlist_create(block_ptr->nodes);
		host_iter = hostlist_iterator_create(hostlist);
		while ((host_name = hostlist_next(host_iter))) {
			number = atoi(host_name + 3);
			start[X] = number / 100;
			start[Y] = (number % 100) / 10;
			start[Z] = (number % 10);
						
			part.total_nodes += set_grid_bgl(start, start, *count);
			free(host_name);
		}
		hostlist_iterator_destroy(host_iter);
		hostlist_destroy(hostlist);
	}

	if(block_ptr->slurm_part_name)
		part.name = block_ptr->slurm_part_name;
	else
		part.name = "no part";
	part.allow_groups = block_ptr->nodes;
	part.root_only = (int) pa_system_ptr->fill_in_value[*count].letter;
	wattron(pa_system_ptr->text_win, 
		COLOR_PAIR(pa_system_ptr->fill_in_value[*count].color));
	_print_text_part(&part, block_ptr);
	wattroff(pa_system_ptr->text_win,
		 COLOR_PAIR(pa_system_ptr->fill_in_value[*count].color));
	(*count)++;
	return SLURM_SUCCESS;
}

#ifdef HAVE_BGL_FILES
static int _post_block_read(void *object, void *arg)
{
	db2_block_info_t *block_ptr = (db2_block_info_t *) object;
	int len = 1024;

	block_ptr->nodes = xmalloc(len);
	while (hostlist_ranged_string(block_ptr->hostlist, len, 
				      block_ptr->nodes) < 0) {
		len *= 2;
		xrealloc(block_ptr->nodes, len);
	}

	return SLURM_SUCCESS;
} 
#endif

static void _read_part_db2(void)
{
#ifdef HAVE_BGL_FILES
	status_t rc;
	static rm_BGL_t *bgl = NULL;
	rm_BP_t *my_bp;
	int bp_num, i;
	rm_location_t bp_loc;
	pm_partition_id_t part_id;
	rm_partition_t *part_ptr;
	char bgl_node[16];
	db2_block_info_t *block_ptr;
	//block_t *block_ptr;
	char *user_name;
	
	if (bgl) {
		/* if we have a single base partition, we can't 
		 * run rm_bgl_free, so just read the data once 
		 * and never free it. There is also no sense in
		 * processing it again either. */
		list_for_each(block_list, _clear_printed_flag, NULL);
		return;
	} else {
		if (!getenv("DB2INSTANCE") || !getenv("VWSPATH")) {
			fprintf(stderr, "Missing DB2INSTANCE or VWSPATH env var.\n"
				"Execute 'db2profile'\n");
			return;
		}
		
		if ((rc = rm_set_serial(BGL_SERIAL)) != STATUS_OK) {
			error("rm_set_serial(): %s\n", bgl_err_str(rc));
			return;
		}
	      
		if ((rc = rm_get_BGL(&bgl)) != STATUS_OK) {
			error("rm_get_BGL(): %s\n", bgl_err_str(rc));
			return;
		}
	}

	if (block_list) {
		/* clear the old list */
		
		list_delete_all(block_list, _list_match_all, NULL);
	} else {
		block_list = list_create(_block_list_del);
		if (!block_list) {
			fprintf(stderr, "malloc error\n");
			return;
		}
	}
	
	if ((rc = rm_get_data(bgl, RM_BPNum, &bp_num)) != STATUS_OK) {
		//fprintf(stderr, "rm_get_data(RM_BPNum): %s\n", bgl_err_str(rc));
		bp_num = 0;
	}

	for (i=0; i<bp_num; i++) {
		if (i) {
			if ((rc = rm_get_data(bgl, RM_NextBP, &my_bp))
			    != STATUS_OK) {
				/* fprintf(stderr, "rm_get_data(RM_NextBP): %s\n", */
/* 					bgl_err_str(rc)); */
				break;
			}
		} else {
			if ((rc = rm_get_data(bgl, RM_FirstBP, &my_bp))
			    != STATUS_OK) {
				/* fprintf(stderr, "rm_get_data(RM_FirstBP): %s\n", */
/* 					bgl_err_str(rc)); */
				break;
			}
		}
		if ((rc = rm_get_data(my_bp, RM_BPLoc, &bp_loc))
		    != STATUS_OK) {
			/* fprintf(stderr, "rm_get_data(RM_BPLoc): %s\n", */
/* 				bgl_err_str(rc)); */
			continue;
		}
		snprintf(bgl_node, sizeof(bgl_node), "bgl%d%d%d",
			 bp_loc.X, bp_loc.Y, bp_loc.Z);
		
		if ((rc = rm_get_data(my_bp, RM_BPPartID, &part_id))
		    != STATUS_OK) {
			/* fprintf(stderr, "rm_get_data(RM_BPPartId): %s\n", */
/* 				bgl_err_str(rc)); */
			continue;
		}
		if (!part_id || (part_id[0] == '\0')) {
#if 1
			/* this is a problem on the 128 c-node system */
			part_id = "LLNL_128_16";
#else
			fprintf(stderr, "BPPartId=NULL\n");
			continue;
#endif
		}
		//if(!strncmp("RMP",part_id,3)) {
			block_ptr = list_find_first(block_list,
						    _part_list_find, part_id);
		
			if (!block_ptr) {
				/* New BGL partition record */
				rm_connection_type_t conn_type;
				rm_partition_mode_t node_use;
					if ((rc = rm_get_partition(part_id, &part_ptr))
					    != STATUS_OK) {
						/*fprintf(stderr, "rm_get_partition(%s): %s\n",
						  part_id, bgl_err_str(rc));*/
						continue;
					}
		
				block_ptr = xmalloc(sizeof(db2_block_info_t));
				list_push(block_list, block_ptr);
				block_ptr->bgl_block_name = xstrdup(part_id);

				if ((rc = rm_get_data(part_ptr, 
						      RM_PartitionState, 
						      &block_ptr->state)) != STATUS_OK) {
					/* fprintf(stderr, "rm_get_data(" */
/* 					"RM_PartitionState): %s\n", */
/* 					bgl_err_str(rc)); */
				}				
			
				if ((rc = rm_get_data(part_ptr, 
						      RM_PartitionUserName, 
						      &user_name)) != STATUS_OK) {
					/* fprintf(stderr, "rm_get_data(" */
/* 					"RM_PartitionUserName): %s\n", */
/* 					bgl_err_str(rc)); */
				} else
					block_ptr->bgl_user_name = xstrdup(user_name);
			
				if ((rc = rm_get_data(part_ptr,
						      RM_PartitionConnection,
						      &conn_type)) != STATUS_OK) {
					/* fprintf(stderr, "rm_get_data(" */
/* 					"RM_PartitionConnection): %s\n", */
/* 					bgl_err_str(rc)); */
					block_ptr->bgl_conn_type = SELECT_NAV;
				} else
					block_ptr->bgl_conn_type = conn_type;
				if ((rc = rm_get_data(part_ptr, RM_PartitionMode,
						      &node_use))
				    != STATUS_OK) {
					/* fprintf(stderr, "rm_get_data(" */
/* 					"RM_PartitionMode): %s\n", */
/* 					bgl_err_str(rc)); */
					block_ptr->bgl_node_use = SELECT_NAV_MODE;
				} else
					block_ptr->bgl_node_use = node_use;
				if ((rc = rm_free_partition(part_ptr)) != STATUS_OK) {
					/* fprintf(stderr, "rm_free_partition(): %s\n", */
/* 					bgl_err_str(rc)); */
				}
				block_ptr->hostlist = hostlist_create(bgl_node);
			} else {
				/* Add node name to existing BGL partition record */
				hostlist_push(block_ptr->hostlist, bgl_node);
			}
			//	}
	}

	/* perform post-processing for each bluegene partition */
	list_for_each(block_list, _post_block_read, NULL);

	slurm_rm_free_BGL(bgl);
#endif
}


static char* _convert_conn_type(enum connection_type conn_type)
{
	switch (conn_type) {
	case (SELECT_MESH):
		return "MESH";
	case (SELECT_TORUS):
		return "TORUS";
	case (SELECT_NAV):
		return "NAV";
	}
	return "?";
}

static char* _convert_node_use(enum node_use_type node_use)
{
	switch (node_use) {
	case (SELECT_COPROCESSOR_MODE):
		return "COPROCESSOR";
	case (SELECT_VIRTUAL_NODE_MODE):
		return "VIRTUAL";
	case (SELECT_NAV_MODE):
		return "NAV";
	}
	return "?";
}

