/*
 * Copyright (c) 2010 Stanislav Kozina
 * Copyright (c) 2010 Martin Decky
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * - The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/** @addtogroup top
 * @brief Top utility.
 * @{
 */
/**
 * @file
 */

#include <stdio.h>
#include <stdlib.h>
#include <task.h>
#include <time.h>
#include <errno.h>
#include <gsort.h>
#include <str.h>
#include "screen.h"
#include "top.h"
#include "top_gui.h"

#define NAME  "top"

#define UPDATE_INTERVAL  1

#define DAY     86400
#define HOUR    3600
#define MINUTE  60

typedef enum {
	OP_TASKS,
	OP_IPC,
	OP_EXCS,
} op_mode_t;

static const column_t task_columns[] = {
	{ "ID",    't',  8 },
	{ "%CPU",  'c', 10 },
	{ "Mem",   'm', 12 },
	{ "Ime",   'd',  0 },
};

/* Sistemski zadaci — ne prikazujemo ih u GUI listi */
static const char *system_task_names[] = {
    "kernel", "initrd", "taskmon", "vfs", "devman",
    "logger", "loc", "clipboard", "klog", "kconsole",
    "init", "loader", "boot", "ns", "bd", "vbd",
    "audio", "console", "hid", "usb", "pci", "net",
    "tcp", "udp", "dhcp", "ping", "wndmgr", "ddf","ns","rd","locfs","uhci",
	"ext4fs","exfat",
    NULL
};

static bool is_system_task(const char *name)
{
    for (size_t i = 0; system_task_names[i] != NULL; i++) {
        if (str_cmp(name, system_task_names[i]) == 0)
            return true;
    }
    return false;
}


static const column_t ipc_columns[] = {
	{ "idZadat",  't', 8 },
	{ "cls snt", 'c', 9 },
	{ "cls rcv", 'C', 9 },
	{ "ans snt", 'a', 9 },
	{ "ans rcv", 'A', 9 },
	{ "prosledj", 'f', 9 },
	{ "naziv",    'd', 0 },
};

enum {
	IPC_COL_TASKID = 0,
	IPC_COL_CLS_SNT,
	IPC_COL_CLS_RCV,
	IPC_COL_ANS_SNT,
	IPC_COL_ANS_RCV,
	IPC_COL_FORWARD,
	IPC_COL_NAME,
	IPC_NUM_COLUMNS,
};

static const column_t exception_columns[] = {
	{ "exc",         'e',  8 },
	{ "broj",       'n', 10 },
	{ "%Broj",      'N',  8 },
	{ "ciklusa",      'c', 10 },
	{ "%Ciklusa",     'C',  9 },
	{ "opis", 'd',  0 },
};

enum {
	EXCEPTION_COL_ID = 0,
	EXCEPTION_COL_COUNT,
	EXCEPTION_COL_PERCENT_COUNT,
	EXCEPTION_COL_CYCLES,
	EXCEPTION_COL_PERCENT_CYCLES,
	EXCEPTION_COL_DESCRIPTION,
	EXCEPTION_NUM_COLUMNS,
};

screen_mode_t screen_mode = SCREEN_TABLE;
static op_mode_t op_mode = OP_TASKS;
 size_t sort_column = TASK_COL_CPU;   /* ili TASK_COL_CPU */
 int sort_reverse = -1;

static bool excs_all = false;

 const char *read_data(data_t *target)
{
	/* Initialize data */
	target->load = NULL;
	target->cpus = NULL;
	target->cpus_perc = NULL;
	target->tasks = NULL;
	target->tasks_perc = NULL;
	target->threads = NULL;
	target->exceptions = NULL;
	target->exceptions_perc = NULL;
	target->physmem = NULL;
	target->ucycles_diff = NULL;
	target->kcycles_diff = NULL;
	target->ecycles_diff = NULL;
	target->ecount_diff = NULL;
	target->table.name = NULL;
	target->table.num_columns = 0;
	target->table.columns = NULL;
	target->table.num_fields = 0;
	target->table.fields = NULL;

	/* Get current time */
	struct timespec time;
	getrealtime(&time);

	target->hours = (time.tv_sec % DAY) / HOUR;
	target->minutes = (time.tv_sec % HOUR) / MINUTE;
	target->seconds = time.tv_sec % MINUTE;

	/* Get uptime */
	struct timespec uptime;
	getuptime(&uptime);

	target->udays = uptime.tv_sec / DAY;
	target->uhours = (uptime.tv_sec % DAY) / HOUR;
	target->uminutes = (uptime.tv_sec % HOUR) / MINUTE;
	target->useconds = uptime.tv_sec % MINUTE;

	/* Get load */
	target->load = stats_get_load(&(target->load_count));
	if (target->load == NULL)
		return "Ne mogu da dobijem ucitani sistem";

	/* Get CPUs */
	target->cpus = stats_get_cpus(&(target->cpus_count));
	if (target->cpus == NULL)
		return "Ne mogu da dobijem CPU-ove";

	target->cpus_perc =
	    (perc_cpu_t *) calloc(target->cpus_count, sizeof(perc_cpu_t));
	if (target->cpus_perc == NULL)
		return "Nedovoljno memorije za CPU ulepsavanje";

	/* Get tasks */
	target->tasks = stats_get_tasks(&(target->tasks_count));
	if (target->tasks == NULL)
		return "Ne mogu da dobijem zadatke";

	target->tasks_perc =
	    (perc_task_t *) calloc(target->tasks_count, sizeof(perc_task_t));
	if (target->tasks_perc == NULL)
		return "Nedovoljno memorije za excepcije ulepsavanja zadataka";

	/* Get threads */
	target->threads = stats_get_threads(&(target->threads_count));
	if (target->threads == NULL)
		return "Ne mogu da dobijem veze";

	/* Get Exceptions */
	target->exceptions = stats_get_exceptions(&(target->exceptions_count));
	if (target->exceptions == NULL)
		return "Ne mogu da dobijem ekscepcije";

	target->exceptions_perc =
	    (perc_exc_t *) calloc(target->exceptions_count, sizeof(perc_exc_t));
	if (target->exceptions_perc == NULL)
		return "Nedovoljno memorije za excepcije ulepsavanja eksepcija";

	/* Get physical memory */
	target->physmem = stats_get_physmem();
	if (target->physmem == NULL)
		return "Ne mogu da dobijem fizicku memriju";

	target->ucycles_diff = calloc(target->tasks_count,
	    sizeof(uint64_t));
	if (target->ucycles_diff == NULL)
		return "Nedovoljno memorije za excepcije ulepsavanja korisnika";

	/* Allocate memory for computed values */
	target->kcycles_diff = calloc(target->tasks_count,
	    sizeof(uint64_t));
	if (target->kcycles_diff == NULL)
		return "Nedovoljno memorije za kernel utilization";

	target->ecycles_diff = calloc(target->exceptions_count,
	    sizeof(uint64_t));
	if (target->ecycles_diff == NULL)
		return "Nedovoljno memorije za excepcije ulepsavanja ciklusa";

	target->ecount_diff = calloc(target->exceptions_count,
	    sizeof(uint64_t));
	if (target->ecount_diff == NULL)
		return "Nedovoljno memorije za excepcije ulepsavanja brojenja";

	return NULL;
}




/** Computes percentage differencies from old_data to new_data
 *
 * @param old_data Pointer to old data strucutre.
 * @param new_data Pointer to actual data where percetages are stored.
 *
 */
 void __attribute__((unused)) compute_percentages(data_t *old_data, data_t *new_data)
{
	/*
	 * For each CPU: Compute total cycles and divide it between
	 * user and kernel
	 */

	size_t i;
	for (i = 0; i < new_data->cpus_count; i++) {
		uint64_t idle =
		    new_data->cpus[i].idle_cycles - old_data->cpus[i].idle_cycles;
		uint64_t busy =
		    new_data->cpus[i].busy_cycles - old_data->cpus[i].busy_cycles;
		uint64_t sum = idle + busy;

		FRACTION_TO_FLOAT(new_data->cpus_perc[i].idle, idle * 100, sum);
		FRACTION_TO_FLOAT(new_data->cpus_perc[i].busy, busy * 100, sum);
	}

	/* For all tasks compute sum and differencies of all cycles */

	uint64_t virtmem_total = 0;
	uint64_t resmem_total = 0;
	uint64_t ucycles_total = 0;
	uint64_t kcycles_total = 0;

	for (i = 0; i < new_data->tasks_count; i++) {
		/* Match task with the previous instance */

		bool found = false;
		size_t j;
		for (j = 0; j < old_data->tasks_count; j++) {
			if (new_data->tasks[i].task_id == old_data->tasks[j].task_id) {
				found = true;
				break;
			}
		}

		if (!found) {
			/* This is newly borned task, ignore it */
			new_data->ucycles_diff[i] = 0;
			new_data->kcycles_diff[i] = 0;
			continue;
		}

		new_data->ucycles_diff[i] =
		    new_data->tasks[i].ucycles - old_data->tasks[j].ucycles;
		new_data->kcycles_diff[i] =
		    new_data->tasks[i].kcycles - old_data->tasks[j].kcycles;

		virtmem_total += new_data->tasks[i].virtmem;
		resmem_total += new_data->tasks[i].resmem;
		ucycles_total += new_data->ucycles_diff[i];
		kcycles_total += new_data->kcycles_diff[i];
	}

	/* For each task compute percential change */

	for (i = 0; i < new_data->tasks_count; i++) {
		FRACTION_TO_FLOAT(new_data->tasks_perc[i].virtmem,
		    new_data->tasks[i].virtmem * 100, virtmem_total);
		FRACTION_TO_FLOAT(new_data->tasks_perc[i].resmem,
		    new_data->tasks[i].resmem * 100, resmem_total);
		FRACTION_TO_FLOAT(new_data->tasks_perc[i].ucycles,
		    new_data->ucycles_diff[i] * 100, ucycles_total);
		FRACTION_TO_FLOAT(new_data->tasks_perc[i].kcycles,
		    new_data->kcycles_diff[i] * 100, kcycles_total);
	}

	/* For all exceptions compute sum and differencies of cycles */

	uint64_t ecycles_total = 0;
	uint64_t ecount_total = 0;

	for (i = 0; i < new_data->exceptions_count; i++) {
		/*
		 * March exception with the previous instance.
		 * This is quite paranoid since exceptions do not
		 * usually disappear, but it does not hurt.
		 */

		bool found = false;
		size_t j;
		for (j = 0; j < old_data->exceptions_count; j++) {
			if (new_data->exceptions[i].id == old_data->exceptions[j].id) {
				found = true;
				break;
			}
		}

		if (!found) {
			/* This is a new exception, ignore it */
			new_data->ecycles_diff[i] = 0;
			new_data->ecount_diff[i] = 0;
			continue;
		}

		new_data->ecycles_diff[i] =
		    new_data->exceptions[i].cycles - old_data->exceptions[j].cycles;
		new_data->ecount_diff[i] =
		    new_data->exceptions[i].count - old_data->exceptions[i].count;

		ecycles_total += new_data->ecycles_diff[i];
		ecount_total += new_data->ecount_diff[i];
	}

	/* For each exception compute percential change */

	for (i = 0; i < new_data->exceptions_count; i++) {
		FRACTION_TO_FLOAT(new_data->exceptions_perc[i].cycles,
		    new_data->ecycles_diff[i] * 100, ecycles_total);
		FRACTION_TO_FLOAT(new_data->exceptions_perc[i].count,
		    new_data->ecount_diff[i] * 100, ecount_total);
	}
}

static int cmp_data(void *a, void *b, void *arg)
{
	field_t *fa = (field_t *)a + sort_column;
	field_t *fb = (field_t *)b + sort_column;

	if (fa->type > fb->type)
		return 1 * sort_reverse;

	if (fa->type < fb->type)
		return -1 * sort_reverse;

	switch (fa->type) {
	case FIELD_EMPTY:
		return 0;
	case FIELD_UINT_SUFFIX_BIN: /* fallthrough */
	case FIELD_UINT_SUFFIX_DEC: /* fallthrough */
	case FIELD_UINT:
		if (fa->uint > fb->uint)
			return 1 * sort_reverse;
		if (fa->uint < fb->uint)
			return -1 * sort_reverse;
		return 0;
		case FIELD_PERCENT: {
			long double va = (long double)fa->fixed.upper /
							 (long double)fa->fixed.lower;
			long double vb = (long double)fb->fixed.upper /
							 (long double)fb->fixed.lower;
			if (va > vb) return 1 * sort_reverse;
			if (va < vb) return -1 * sort_reverse;
			return 0;
		}
	case FIELD_STRING:
		return str_cmp(fa->string, fb->string) * sort_reverse;
	}

	return 0;
}

void __attribute__((unused)) sort_table(table_t *table)
{
	if (sort_column >= table->num_columns)
		sort_column = 0;
	/* stable sort is probably best, so we use gsort */
	gsort((void *) table->fields, table->num_fields / table->num_columns,
	    sizeof(field_t) * table->num_columns, cmp_data, NULL);
}

const char *fill_task_table(data_t *data)
{
    data->table.name = "Zadaci";
    data->table.num_columns = TASK_NUM_COLUMNS;
    data->table.columns = task_columns;

    /* Prvo izbroj koliko zadataka prolazi filter */
    size_t count = 0;
    for (size_t i = 0; i < data->tasks_count; i++) {
        if (!is_system_task(data->tasks[i].name))
            count++;
    }

    data->table.num_fields = count * TASK_NUM_COLUMNS;
    data->table.fields = calloc(data->table.num_fields,
        sizeof(field_t));
    if (data->table.fields == NULL)
        return "Nedovoljno memorije za polja tabela";

    field_t *field = data->table.fields;
    for (size_t i = 0; i < data->tasks_count; i++) {
        stats_task_t *task = &data->tasks[i];
        perc_task_t *perc = &data->tasks_perc[i];

        /* Preskoči sistemske zadatke */
        if (is_system_task(task->name))
            continue;

        /* ID */
        field[TASK_COL_ID].type = FIELD_UINT;
        field[TASK_COL_ID].uint = task->task_id;

        /* %CPU = %korisnik + %kern (sabiranje razlomaka) */
        field[TASK_COL_CPU].type = FIELD_PERCENT;
        field[TASK_COL_CPU].fixed.upper =
            perc->ucycles.upper * perc->kcycles.lower +
            perc->kcycles.upper * perc->ucycles.lower;
        field[TASK_COL_CPU].fixed.lower =
            perc->ucycles.lower * perc->kcycles.lower;

        /* Memorija = rezidentno */
        field[TASK_COL_MEM].type = FIELD_UINT_SUFFIX_BIN;
        field[TASK_COL_MEM].uint = task->resmem;

        /* Ime */
        field[TASK_COL_NAME].type = FIELD_STRING;
        field[TASK_COL_NAME].string = task->name;

        field += TASK_NUM_COLUMNS;
    }

    return NULL;
}

const char *fill_ipc_table(data_t *data)
{
	data->table.name = "IPC";
	data->table.num_columns = IPC_NUM_COLUMNS;
	data->table.columns = ipc_columns;
	data->table.num_fields = data->tasks_count * IPC_NUM_COLUMNS;
	data->table.fields = calloc(data->table.num_fields,
	    sizeof(field_t));
	if (data->table.fields == NULL)
		return "Nedovoljno memorije za polja tabela";

	field_t *field = data->table.fields;
	for (size_t i = 0; i < data->tasks_count; i++) {
		field[IPC_COL_TASKID].type = FIELD_UINT;
		field[IPC_COL_TASKID].uint = data->tasks[i].task_id;
		field[IPC_COL_CLS_SNT].type = FIELD_UINT_SUFFIX_DEC;
		field[IPC_COL_CLS_SNT].uint = data->tasks[i].ipc_info.call_sent;
		field[IPC_COL_CLS_RCV].type = FIELD_UINT_SUFFIX_DEC;
		field[IPC_COL_CLS_RCV].uint = data->tasks[i].ipc_info.call_received;
		field[IPC_COL_ANS_SNT].type = FIELD_UINT_SUFFIX_DEC;
		field[IPC_COL_ANS_SNT].uint = data->tasks[i].ipc_info.answer_sent;
		field[IPC_COL_ANS_RCV].type = FIELD_UINT_SUFFIX_DEC;
		field[IPC_COL_ANS_RCV].uint = data->tasks[i].ipc_info.answer_received;
		field[IPC_COL_FORWARD].type = FIELD_UINT_SUFFIX_DEC;
		field[IPC_COL_FORWARD].uint = data->tasks[i].ipc_info.forwarded;
		field[IPC_COL_NAME].type = FIELD_STRING;
		field[IPC_COL_NAME].string = data->tasks[i].name;
		field += IPC_NUM_COLUMNS;
	}

	return NULL;
}

const char *fill_exception_table(data_t *data)
{
	data->table.name = "Ekscepcije";
	data->table.num_columns = EXCEPTION_NUM_COLUMNS;
	data->table.columns = exception_columns;
	data->table.num_fields = data->exceptions_count *
	    EXCEPTION_NUM_COLUMNS;
	data->table.fields = calloc(data->table.num_fields, sizeof(field_t));
	if (data->table.fields == NULL)
		return "Nedovoljno memorije za polja tabela";

	field_t *field = data->table.fields;
	for (size_t i = 0; i < data->exceptions_count; i++) {
		if (!excs_all && !data->exceptions[i].hot)
			continue;
		field[EXCEPTION_COL_ID].type = FIELD_UINT;
		field[EXCEPTION_COL_ID].uint = data->exceptions[i].id;
		field[EXCEPTION_COL_COUNT].type = FIELD_UINT_SUFFIX_DEC;
		field[EXCEPTION_COL_COUNT].uint = data->exceptions[i].count;
		field[EXCEPTION_COL_PERCENT_COUNT].type = FIELD_PERCENT;
		field[EXCEPTION_COL_PERCENT_COUNT].fixed = data->exceptions_perc[i].count;
		field[EXCEPTION_COL_CYCLES].type = FIELD_UINT_SUFFIX_DEC;
		field[EXCEPTION_COL_CYCLES].uint = data->exceptions[i].cycles;
		field[EXCEPTION_COL_PERCENT_CYCLES].type = FIELD_PERCENT;
		field[EXCEPTION_COL_PERCENT_CYCLES].fixed = data->exceptions_perc[i].cycles;
		field[EXCEPTION_COL_DESCRIPTION].type = FIELD_STRING;
		field[EXCEPTION_COL_DESCRIPTION].string = data->exceptions[i].desc;
		field += EXCEPTION_NUM_COLUMNS;
	}

	/* in case any cold exceptions were ignored */
	data->table.num_fields = field - data->table.fields;

	return NULL;
}

const char *fill_table(data_t *data)
{
	if (data->table.fields != NULL) {
		free(data->table.fields);
		data->table.fields = NULL;
	}

	switch (op_mode) {
	case OP_TASKS:
		return fill_task_table(data);
	case OP_IPC:
		return fill_ipc_table(data);
	case OP_EXCS:
		return fill_exception_table(data);
	}
	return NULL;
}

void  free_data(data_t *target)
{
	if (target->load != NULL)
		free(target->load);

	if (target->cpus != NULL)
		free(target->cpus);

	if (target->cpus_perc != NULL)
		free(target->cpus_perc);

	if (target->tasks != NULL)
		free(target->tasks);

	if (target->tasks_perc != NULL)
		free(target->tasks_perc);

	if (target->threads != NULL)
		free(target->threads);

	if (target->exceptions != NULL)
		free(target->exceptions);

	if (target->exceptions_perc != NULL)
		free(target->exceptions_perc);

	if (target->physmem != NULL)
		free(target->physmem);

	if (target->ucycles_diff != NULL)
		free(target->ucycles_diff);

	if (target->kcycles_diff != NULL)
		free(target->kcycles_diff);

	if (target->ecycles_diff != NULL)
		free(target->ecycles_diff);

	if (target->ecount_diff != NULL)
		free(target->ecount_diff);

	if (target->table.fields != NULL)
		free(target->table.fields);
}

int main(int argc, char *argv[])
{
    return guitop_main(argc, argv);
}

/** @}
 */
