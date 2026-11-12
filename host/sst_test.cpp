// Test della CPU contro SingleStepTests/8088 (convertiti con tools/sst_convert.py)
#include "../core/cpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <vector>
#include <string>
#include <algorithm>

static u8 memory[0x100000];
static u8 wmapAll[256];

class TestBus : public CpuBus {
public:
	u8 in8(u16) override { return 0xFF; }
	void out8(u16, u8) override {}
	int irqAck() override { return -1; }
};

int main(int argc, char **argv)
{
	const char *dir = argc > 1 ? argv[1] : "sstconv";
	bool verbose = argc > 2;
	memset(wmapAll, 1, sizeof wmapAll);
	TestBus bus;
	CPU cpu;
	cpu.bus = &bus; cpu.mem = memory; cpu.wmap = wmapAll;
	cpu.repChunking = false;
	std::vector<std::string> files;
	DIR *d = opendir(dir);
	while (dirent *e = readdir(d)) if (strstr(e->d_name, ".txt")) files.push_back(e->d_name);
	closedir(d);
	std::sort(files.begin(), files.end());
	int totalFail = 0, totalTests = 0, badFiles = 0;
	for (auto &fn : files) {
		FILE *f = fopen((std::string(dir) + "/" + fn).c_str(), "r");
		char key[32], status[32]; int mask, n;
		if (fscanf(f, "%31s %d %31s %d", key, &mask, status, &n) != 4) { fclose(f); continue; }
		int fails = 0; bool printed = false;
		for (int t = 0; t < n; t++) {
			unsigned ir[14], fr[14];
			for (int i = 0; i < 14; i++) if (fscanf(f, "%u", &ir[i]) != 1) return 1;
			for (int i = 0; i < 14; i++) if (fscanf(f, "%u", &fr[i]) != 1) return 1;
			int nr; std::vector<std::pair<unsigned, unsigned>> iram, fram;
			if (fscanf(f, "%d", &nr) != 1) return 1;
			for (int i = 0; i < nr; i++) { unsigned a, v; if (fscanf(f, "%u %u", &a, &v) != 2) return 1; iram.push_back({a, v}); }
			if (fscanf(f, "%d", &nr) != 1) return 1;
			for (int i = 0; i < nr; i++) { unsigned a, v; if (fscanf(f, "%u %u", &a, &v) != 2) return 1; fram.push_back({a, v}); }
			for (auto &p : iram) memory[p.first] = p.second;
			cpu.reset();
			// ordine: ax bx cx dx cs ss ds es sp bp si di ip flags
			cpu.r[AX] = ir[0]; cpu.r[BX] = ir[1]; cpu.r[CX] = ir[2]; cpu.r[DX] = ir[3];
			cpu.s[CS] = ir[4]; cpu.s[SS] = ir[5]; cpu.s[DS] = ir[6]; cpu.s[ES] = ir[7];
			cpu.r[SP] = ir[8]; cpu.r[BP] = ir[9]; cpu.r[SI] = ir[10]; cpu.r[DI] = ir[11];
			cpu.ip = ir[12]; cpu.flags = ir[13];
			cpu.step();
			unsigned got[14] = { cpu.r[AX], cpu.r[BX], cpu.r[CX], cpu.r[DX], cpu.s[CS], cpu.s[SS], cpu.s[DS], cpu.s[ES],
				cpu.r[SP], cpu.r[BP], cpu.r[SI], cpu.r[DI], cpu.ip, cpu.packedFlags() };
			bool ok = true;
			for (int i = 0; i < 14; i++) {
				unsigned a = got[i], b = fr[i];
				if (i == 13) { a &= mask; b &= mask; }
				if (a != b) ok = false;
			}
			unsigned fa = (fr[5] * 16u + ((fr[8] + 4) & 0xFFFF)) & 0xFFFFF;
			for (auto &p : fram) if (memory[p.first] != p.second) {
				// le flag salvate sullo stack (eccezione divisione) contengono bit indefiniti
				if (p.first == fa && ((memory[p.first] ^ p.second) & mask & 0xFF) == 0) continue;
				if (p.first == fa + 1 && ((memory[p.first] ^ p.second) & (mask >> 8)) == 0) continue;
				ok = false;
			}
			if (!ok) {
				fails++;
				if (verbose && !printed) {
					printed = true;
					static const char *nm[14] = {"ax","bx","cx","dx","cs","ss","ds","es","sp","bp","si","di","ip","fl"};
					printf("  %s test %d:\n", key, t);
					for (int i = 0; i < 14; i++) if (got[i] != fr[i]) printf("    %s got %04x exp %04x (init %04x)\n", nm[i], got[i], fr[i], ir[i]);
					for (auto &p : fram) if (memory[p.first] != p.second) printf("    mem %05x got %02x exp %02x\n", p.first, memory[p.first], p.second);
					printf("    bytes:"); for (auto &p : iram) if (p.first >= ir[4]*16u+ir[12] && p.first < ir[4]*16u+ir[12]+6) printf(" %02x", p.second); printf("\n");
				}
			}
		}
		fclose(f);
		totalTests += n; totalFail += fails;
		if (fails) { badFiles++; printf("%-8s %-12s fail %4d / %d\n", key, status, fails, n); }
	}
	printf("TOTALE: %d test, %d falliti, %d opcode con errori su %zu\n", totalTests, totalFail, badFiles, files.size());
	return 0;
}
