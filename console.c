#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>

#ifdef _MSC_VER
#pragma comment(lib, "pdh.lib")
#endif

#ifndef ADD_COUNTER_FN
#define ADD_COUNTER_FN PdhAddEnglishCounterW
#endif

#define timeToRefresh 1000
#define BYTES_PER_MB (1024.0 * 1024.0)

static void print_error(const char *where, PDH_STATUS status)
{
	fprintf(stderr, "%s failed: 0x%08lx\n", where, (unsigned long)status);
}

static int add_counter_checked(PDH_HQUERY query, const wchar_t *path, PDH_HCOUNTER *out, const char *label)
{
	PDH_STATUS status = ADD_COUNTER_FN(query, path, 0, out);
	if (status != ERROR_SUCCESS) {
		print_error(label, status);
		return -1;
	}
	return 0;
}

static int get_double_value(PDH_HCOUNTER counter, double *out_value, const char *label)
{
	PDH_FMT_COUNTERVALUE val;
	DWORD type = 0;
	PDH_STATUS status = PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, &type, &val);
	if (status != ERROR_SUCCESS) {
		print_error(label, status);
		return -1;
	}
	*out_value = val.doubleValue;
	return 0;
}

static int collect_per_core(PDH_HCOUNTER counter, PDH_FMT_COUNTERVALUE_ITEM_W **out_items, DWORD *out_count)
{
	DWORD item_count = 0;
	DWORD buffer_size = 0;
	PDH_STATUS status = PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &buffer_size, &item_count, NULL);
	if ((DWORD)status != PDH_MORE_DATA) {
		return -1;
	}
	PDH_FMT_COUNTERVALUE_ITEM_W *items = (PDH_FMT_COUNTERVALUE_ITEM_W*)malloc(buffer_size);
	if (!items) {
		print_error("malloc", ERROR_OUTOFMEMORY);
		return -1;
	}
	status = PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &buffer_size, &item_count, items);
	if (status != ERROR_SUCCESS) {
		free(items);
		print_error("PdhGetFormattedCounterArrayW CPU per-core", status);
		return -1;
	}
	*out_items = items;
	*out_count = item_count;
	return 0;
}

static void render_view(double cpu_total,
	PDH_FMT_COUNTERVALUE_ITEM_W *items,
	DWORD item_count,
	double mem_pct,
	double avail_mb,
	double committed_bytes,
	double commit_limit_bytes)
{
	system("cls");
	printf("CPU Total: %6.2f%%\n", cpu_total);
	if (items && item_count > 0) {
		printf("Per-Core:\n");
		for (DWORD i = 0; i < item_count; ++i) {
			if (items[i].szName && wcscmp(items[i].szName, L"_Total") == 0) {
				continue;
			}
			const wchar_t *name = items[i].szName ? items[i].szName : L"?";
			wprintf(L"  CPU %-6ls: %6.2f%%\n", name, items[i].FmtValue.doubleValue);
		}
	}
	double committed_mb = committed_bytes / BYTES_PER_MB;
	double commit_limit_mb = commit_limit_bytes / BYTES_PER_MB;
	printf("\nMemory: %6.2f%% (Avail: %.0f MB, Committed: %.0f MB / Limit: %.0f MB)\n",
		mem_pct, avail_mb, committed_mb, commit_limit_mb);
}

int main(void)
{
	PDH_STATUS status;
	PDH_HQUERY query = NULL;
	PDH_HCOUNTER cpu_counter = NULL;
	PDH_HCOUNTER cpu_per_core_counter = NULL;
	PDH_HCOUNTER mem_counter = NULL;
	PDH_HCOUNTER mem_avail_mb_counter = NULL;
	PDH_HCOUNTER mem_committed_bytes_counter = NULL;
	PDH_HCOUNTER mem_commit_limit_bytes_counter = NULL;
	const wchar_t *cpu_path = L"\\Processor(_Total)\\% Processor Time";
	const wchar_t *cpu_per_core_path = L"\\Processor(*)\\% Processor Time";
	const wchar_t *mem_path = L"\\Memory\\% Committed Bytes In Use";
	const wchar_t *mem_avail_mb_path = L"\\Memory\\Available MBytes";
	const wchar_t *mem_committed_bytes_path = L"\\Memory\\Committed Bytes";
	const wchar_t *mem_commit_limit_bytes_path = L"\\Memory\\Commit Limit";

	status = PdhOpenQueryW(NULL, 0, &query);
	if (status != ERROR_SUCCESS) {
		print_error("PdhOpenQueryW", status);
		return 1;
	}

	if (add_counter_checked(query, cpu_path, &cpu_counter, "PdhAdd(English)CounterW CPU") < 0) { PdhCloseQuery(query); return 1; }
	if (add_counter_checked(query, cpu_per_core_path, &cpu_per_core_counter, "PdhAdd(English)CounterW CPU per-core") < 0) { PdhCloseQuery(query); return 1; }
	if (add_counter_checked(query, mem_path, &mem_counter, "PdhAdd(English)CounterW MEM") < 0) { PdhCloseQuery(query); return 1; }
	if (add_counter_checked(query, mem_avail_mb_path, &mem_avail_mb_counter, "PdhAdd(English)CounterW MEM Available MB") < 0) { PdhCloseQuery(query); return 1; }
	if (add_counter_checked(query, mem_committed_bytes_path, &mem_committed_bytes_counter, "PdhAdd(English)CounterW MEM Committed Bytes") < 0) { PdhCloseQuery(query); return 1; }
	if (add_counter_checked(query, mem_commit_limit_bytes_path, &mem_commit_limit_bytes_counter, "PdhAdd(English)CounterW MEM Commit Limit") < 0) { PdhCloseQuery(query); return 1; }

	status = PdhCollectQueryData(query);
	if (status != ERROR_SUCCESS) {
		print_error("PdhCollectQueryData (warmup)", status);
		PdhCloseQuery(query);
		return 1;
	}
	Sleep(timeToRefresh);

	printf("Press Ctrl+C to stop.\n");
	for (;;) {
		status = PdhCollectQueryData(query);
		if (status != ERROR_SUCCESS) {
			print_error("PdhCollectQueryData", status);
			break;
		}

		double cpu_total = 0.0;
		double mem_pct = 0.0;
		double avail_mb = 0.0;
		double committed_bytes = 0.0;
		double commit_limit_bytes = 0.0;

		if (get_double_value(cpu_counter, &cpu_total, "PdhGetFormattedCounterValue CPU") < 0) { break; }
		if (get_double_value(mem_counter, &mem_pct, "PdhGetFormattedCounterValue MEM") < 0) { break; }
		if (get_double_value(mem_avail_mb_counter, &avail_mb, "PdhGetFormattedCounterValue MEM Available MB") < 0) { break; }
		if (get_double_value(mem_committed_bytes_counter, &committed_bytes, "PdhGetFormattedCounterValue MEM Committed Bytes") < 0) { break; }
		if (get_double_value(mem_commit_limit_bytes_counter, &commit_limit_bytes, "PdhGetFormattedCounterValue MEM Commit Limit") < 0) { break; }

		PDH_FMT_COUNTERVALUE_ITEM_W *items = NULL;
		DWORD item_count = 0;
		if (collect_per_core(cpu_per_core_counter, &items, &item_count) == 0) {
			render_view(cpu_total, items, item_count, mem_pct, avail_mb, committed_bytes, commit_limit_bytes);
			free(items);
		} else {
			render_view(cpu_total, NULL, 0, mem_pct, avail_mb, committed_bytes, commit_limit_bytes);
		}

		fflush(stdout);
		Sleep(timeToRefresh);
	}

	PdhCloseQuery(query);
	return 0;
}
