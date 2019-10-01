#include "structure.h"

#define MaxQscore 30
#define BlockSize 100
#define BreakPointFreqThr 3
#define INV_TNL_ThrRatio 0.5
#define var_SUB 0 // substitution
#define var_INS 1 // insertion
#define var_DEL 2 // deletion
#define var_INV 3 // inversion
#define var_TNL 4 // translocation
#define var_CNV 5 // copy number variation
#define var_UMR 6 // unmapped region
#define var_NIL 255

typedef struct
{
	int64_t gPos;
	uint16_t left_score;
	uint16_t rigt_score;
} BreakPoint_t;

int BlockNum;
FILE *outFile;
int* BlockDepthArr;
static pthread_mutex_t Lock;
vector<Variant_t> VariantVec;
vector<BreakPoint_t> BreakPointCanVec;
extern map<int64_t, uint16_t> BreakPointMap;
 // TranslocationSiteVec;
extern float FrequencyThr;
extern uint32_t avgReadLength;

extern bool CompByDiscordPos(const DiscordPair_t& p1, const DiscordPair_t& p2);

bool CompByAlnDist(const CoordinatePair_t& p1, const CoordinatePair_t& p2)
{
	return p1.dist < p2.dist;
}

bool CompByDist(const CoordinatePair_t& p1, const CoordinatePair_t& p2)
{
	return p1.dist < p2.dist;
}

bool CompByVarPos(const Variant_t& p1, const Variant_t& p2)
{
	if (p1.gPos == p2.gPos) return p1.VarType < p2.VarType;
	else return p1.gPos < p2.gPos;
}

//pair<char, int> GetMaxItemInProfileColumn(MappingRecord_t& Profile)
//{
//	pair<char, int> p = make_pair('N', 0);
//
//	if (Profile.A > p.second) p = make_pair('A', Profile.A);
//	if (Profile.C > p.second) p = make_pair('C', Profile.C);
//	if (Profile.G > p.second) p = make_pair('G', Profile.G);
//	if (Profile.T > p.second) p = make_pair('T', Profile.T);
//
//	return p;
//}

//int GetIndMapFrq(map<int64_t, map<string, uint16_t> >::iterator IndMapIter, string& ind_str)
//{
//	int freq = 0, max_freq = 0;
//	for (map<string, uint16_t>::iterator iter = IndMapIter->second.begin(); iter != IndMapIter->second.end(); iter++)
//	{
//		freq += iter->second;
//		if (max_freq < iter->second)
//		{
//			ind_str = iter->first;
//			max_freq = iter->second;
//		}
//	}
//	return freq;
//}

int GetPointIndFreq(map<string, uint16_t>& IndMap)
{
	int n = 0;
	for (map<string, uint16_t>::iterator iter = IndMap.begin(); iter != IndMap.end(); iter++) n += iter->second;
	return n;
}

int GetAreaIndFrequency(int64_t gPos, map<int64_t, map<string, uint16_t> >& IndMap, string& ind_str)
{
	int64_t max_pos = 0;
	int freq = 0, max_freq = 0;
	map<string, uint16_t>::iterator it;
	map<int64_t, map<string, uint16_t> >::iterator iter1, iter2;
	
	ind_str.clear();
	for (iter1 = IndMap.lower_bound(gPos - 5), iter2 = IndMap.upper_bound(gPos + 5); iter1 != iter2; iter1++)
	{
		if (abs(iter1->first - gPos) <= 5)
		{
			for (it = iter1->second.begin(); it != iter1->second.end(); it++)
			{
				freq += it->second;
				if (max_freq < it->second)
				{
					ind_str = it->first; 
					max_freq = it->second;
					max_pos = iter1->first;
				}
				else if (max_freq == it->second && it->first.length() > ind_str.length())
				{
					ind_str = it->first;
					max_pos = iter1->first;
				}
			}
		}
	}
	if (gPos == max_pos) return freq;
	else return 0;
}

uint8_t CalQualityScore(int a, int b)
{
	uint8_t qs;
	if (a >= b) qs = MaxQscore;
	else if ((qs= -100 * log10((1.0 - (1.0*a / b)))) > MaxQscore) qs = MaxQscore;

	return qs;
}

void *CalBlockReadDepth(void *arg)
{
	int64_t gPos, end_gPos;
	int bid, end_bid, sum, tid = *((int*)arg);

	bid = (tid == 0 ? 0 : (int)(BlockNum / iThreadNum)*tid); 
	end_bid = (tid == iThreadNum - 1 ? BlockNum : (BlockNum / iThreadNum)*(tid + 1));
	for (; bid < end_bid; bid++)
	{
		gPos = (int64_t)bid*BlockSize; if ((end_gPos = gPos + BlockSize) > GenomeSize) end_gPos = GenomeSize;
		for (sum = 0; gPos < end_gPos; gPos++) sum += GetProfileColumnSize(MappingRecordArr[gPos]);
		if (sum > 0) BlockDepthArr[bid] = sum / BlockSize;
	}
	return (void*)(1);
}

bool CheckDiploidFrequency(int cov, vector<pair<char, int> >& vec)
{
	int sum = vec[0].second + vec[1].second;
	if (sum >= (int)(cov*0.9)) return true;
	else return false;
}

map<int64_t, bool> LoadObservedPos()
{
	int64_t p;
	string str;
	fstream file;
	stringstream ss;
	map<int64_t, bool> m;

	file.open("a.txt", ios_base::in);
	while (!file.eof())
	{
		getline(file, str); if (str == "") break;
		ss.clear(); ss.str(str); ss >> p;
		//m.insert(make_pair(p - 1, (str[str.length()-1] == '+' ? true : false)));
		if(str.find("snp")!=string::npos) m.insert(make_pair(p - 1, (str.find("_t") != string::npos ? true : false)));
		else m.insert(make_pair(p, (str.find("_t") != string::npos ? true : false)));

		if (m.size() == 2000) break;
	}
	file.close();
	return m;
}

bool CheckObsMap(int64_t gPos, map<int64_t, bool>& obs_map)
{
	map<int64_t, bool>::iterator iter1, iter2;

	iter1 = obs_map.lower_bound(gPos - 10);
	iter2 = obs_map.lower_bound(gPos + 10);
	if (abs(iter1->first - gPos) <= 10 || abs(iter2->first - gPos) <= 10) return true;
	else return false;
}

bool CheckBreakPoints(int64_t gPos)
{
	map<int64_t, uint16_t>::iterator iter1, iter2;
	
	iter1 = BreakPointMap.lower_bound(gPos - 10);
	iter2 = BreakPointMap.lower_bound(gPos + 10);
	if (abs(iter1->first - gPos) <= 10 || abs(iter2->first - gPos) <= 10) return true;
	else return false;
}

void ShowMetaInfo()
{
	fprintf(outFile, "##fileformat=VCFv4.3\n");
	fprintf(outFile, "##reference=%s\n", IndexFileName);
	fprintf(outFile, "##source=MapCaller %s\n", VersionStr);
	fprintf(outFile, "##CommandLine=<%s>\n", CmdLine.c_str());
	fprintf(outFile, "##INFO=<ID=AD,Number=1,Type=Integer,Description=\"Allel depth\">\n");
	fprintf(outFile, "##INFO=<ID=DP,Number=1,Type=Integer,Description=\"Total depth\">\n");
	fprintf(outFile, "##INFO=<ID=AF,Number=1,Type=Float,Description=\"Allele frequency\">\n");
	fprintf(outFile, "##FORMAT=<ID=GT,Number=1,Type=String,Description=\"Genotype\">\n");
	fprintf(outFile, "##FILTER=<ID=q10,Description=\"Confidence score below 10\">\n");
	if (bFilter) fprintf(outFile, "##FILTER=<ID=bad_haplotype,Description=\"Variants with variable frequencies on same haplotype\">\n");
	if (bFilter) fprintf(outFile, "##FILTER=<ID=str_contraction,Description=\"Variant appears in repetitive region\">\n");
	fprintf(outFile, "##INFO=<ID=TYPE,Number=1,Type=String,Description=\"The type of allele, either SUBSTITUTE, INSERT, DELETE, or BND.\">\n");
	for (int i = 0; i < iChromsomeNum; i++) fprintf(outFile, "##Contig=<ID=%s,length=%d>\n", ChromosomeVec[i].name, ChromosomeVec[i].len);
	fprintf(outFile, "#CHROM	POS	ID	REF	ALT	QUAL	FILTER	INFO\n");
}

void IdentifyBreakPointCandidates()
{
	BreakPoint_t bp;
	uint32_t total_freq;
	pair<int64_t, uint16_t> p;

	BreakPointMap.insert(make_pair(TwoGenomeSize, 0)); total_freq = 0; p = make_pair(0, 0);
	for (map<int64_t, uint16_t>::iterator iter = BreakPointMap.begin(); iter != BreakPointMap.end(); iter++)
	{
		if (iter->first - p.first > avgReadLength) // break
		{
			if (total_freq >= BreakPointFreqThr)
			{
				bp.gPos = p.first;
				bp.left_score = bp.rigt_score = 0;
				//printf("\tAdd BP_can: %lld (score=%d)\n", (long long)bp.gPos, total_freq);
				BreakPointCanVec.push_back(bp);
			}
			p.first = iter->first;
			total_freq = p.second = iter->second;
		}
		else
		{
			total_freq += iter->second;
			if (p.second < iter->second)
			{
				p.first = iter->first;
				p.second = iter->second;
			}
		}
		//printf("Pos=%lld freq=%d\n", (long long)iter->first, iter->second);
	}
}

int CalRegionCov(int64_t begPos, int64_t endPos)
{
	int cov = 0;
	int64_t gPos;

	if (begPos < 0) begPos = 0; if (endPos > GenomeSize) endPos = GenomeSize - 1;
	if (endPos < begPos) return 0;
	for (gPos = begPos; gPos <= endPos; gPos++) cov += GetProfileColumnSize(MappingRecordArr[gPos]);
	if (endPos >= begPos) return (int)(cov / (endPos - begPos + 1));
	else return 0;
}

void IdentifyTranslocations()
{
	int64_t gPos;
	Variant_t Variant;
	vector<int64_t> vec;
	DiscordPair_t DiscordPair;
	vector<DiscordPair_t>::iterator Iter1, Iter2;
	uint32_t i, j, n, TNLnum, num, score, LCov, RCov, cov_thr, Lscore, Rscore;

	//for (Iter1 = TranslocationSiteVec.begin(); Iter1 != TranslocationSiteVec.end(); Iter1++)  printf("Pos=%lld Dist=%lld\n", (long long)Iter1->gPos, (long long)Iter1->dist);
	for (num = (int)BreakPointCanVec.size(), TNLnum = i = 0; i < num; i++)
	{
		gPos = BreakPointCanVec[i].gPos;
		
		LCov = CalRegionCov(gPos - FragmentSize, gPos - (avgReadLength >> 1));
		cov_thr = BlockDepthArr[(int)(gPos / BlockSize)] >> 1;
		DiscordPair.gPos = gPos - FragmentSize; Iter1 = lower_bound(TranslocationSiteVec.begin(), TranslocationSiteVec.end(), DiscordPair, CompByDiscordPos);
		DiscordPair.gPos = gPos - (avgReadLength >> 1); Iter2 = lower_bound(TranslocationSiteVec.begin(), TranslocationSiteVec.end(), DiscordPair, CompByDiscordPos);
		if (Iter1 == TranslocationSiteVec.end() || Iter2 == TranslocationSiteVec.end()) continue;
		vec.clear(); for (; Iter1 != Iter2; Iter1++) vec.push_back((Iter1->dist / 1000)); 
		sort(vec.begin(), vec.end()); vec.push_back(TwoGenomeSize); n = (int)vec.size();
		for (Lscore = 0, score = j = 1; j < n; j++)
		{
			//printf("Lcan_%d: %lld (score=%d)\n", j + 1, vec[j], score);
			if (vec[j] - vec[j - 1] > 1)
			{
				if (score > Lscore) Lscore = score;
				score = 1;
			}
			else score++;
		}
		if (Lscore < cov_thr || Lscore < (int)(LCov*INV_TNL_ThrRatio)) continue;

		RCov = CalRegionCov(gPos, gPos + FragmentSize);
		DiscordPair.gPos = gPos; Iter1 = upper_bound(TranslocationSiteVec.begin(), TranslocationSiteVec.end(), DiscordPair, CompByDiscordPos);
		DiscordPair.gPos = gPos + FragmentSize; Iter2 = lower_bound(TranslocationSiteVec.begin(), TranslocationSiteVec.end(), DiscordPair, CompByDiscordPos);
		if (Iter1 == TranslocationSiteVec.end() || Iter2 == TranslocationSiteVec.end()) continue;
		vec.clear(); for (; Iter1 != Iter2; Iter1++) vec.push_back((Iter1->dist / 1000));
		sort(vec.begin(), vec.end()); vec.push_back(TwoGenomeSize); n = (int)vec.size();
		for (Rscore = 0, score = j = 1; j < n; j++)
		{
			//printf("Rcan_%d: %lld (score=%d)\n", j + 1, vec[j], score);
			if (vec[j] - vec[j - 1] > 1)
			{
				if (score > Rscore) Rscore = score;
				score = 1;
			}
			else score++;
		}
		if (Rscore < cov_thr || Rscore < (int)(RCov*INV_TNL_ThrRatio)) continue;

		//printf("TNL_can =%lld (Cov=%d vs %d): Lscore=%d Rscore=%d\n", (long long)gPos, LCov, RCov, Lscore, Rscore);
		if (Lscore > 0 && Rscore > 0)
		{
			TNLnum++;
			Variant.gPos = gPos;
			Variant.VarType = var_TNL;
			Variant.NS = Lscore > Rscore ? Lscore : Rscore;
			Variant.qscore = CalQualityScore(Variant.NS, cov_thr);
			VariantVec.push_back(Variant);
		}
	}
	if (TNLnum > 0) inplace_merge(VariantVec.begin(), VariantVec.end() - TNLnum, VariantVec.end(), CompByVarPos);
}

void IdentifyInversions()
{
	int64_t gPos;
	Variant_t Variant;
	vector<int64_t> vec;
	DiscordPair_t DiscordPair;
	vector<DiscordPair_t>::iterator Iter1, Iter2;
	uint32_t i, j, n, LCov, RCov, cov_thr, INVnum, num, score, Lscore, Rscore;

	//for (Iter1 = InversionSiteVec.begin(); Iter1 != InversionSiteVec.end(); Iter1++) printf("Pos=%lld Dist=%lld\n", (long long)Iter1->gPos, (long long)Iter1->dist);
	for (num = (int)BreakPointCanVec.size(), INVnum = i = 0; i < num; i++)
	{
		gPos = BreakPointCanVec[i].gPos; LCov = CalRegionCov(gPos - FragmentSize, gPos - (avgReadLength >> 1));
		cov_thr = BlockDepthArr[(int)(gPos / BlockSize)] >> 1;
		DiscordPair.gPos = gPos - FragmentSize; Iter1 = lower_bound(InversionSiteVec.begin(), InversionSiteVec.end(), DiscordPair, CompByDiscordPos);
		DiscordPair.gPos = gPos - (avgReadLength >> 1); Iter2 = lower_bound(InversionSiteVec.begin(), InversionSiteVec.end(), DiscordPair, CompByDiscordPos);
		if (Iter1 == InversionSiteVec.end() || Iter2 == InversionSiteVec.end()) continue;
		vec.clear(); for (; Iter1 != Iter2; Iter1++) vec.push_back((Iter1->dist / 1000));
		sort(vec.begin(), vec.end()); vec.push_back(TwoGenomeSize);
		for (n = (int)vec.size(), Lscore = 0, score = j = 1; j < n; j++)
		{
			//printf("Lcan_%d: dist=%lld\n", j + 1, vec[j]);
			if (vec[j] - vec[j - 1] > 1)
			{
				if (score > Lscore) Lscore = score;
				score = 1;
			}
			else score++;
		}
		if (Lscore < cov_thr || Lscore < (int)(LCov*INV_TNL_ThrRatio)) continue;

		RCov = CalRegionCov(gPos, gPos + FragmentSize);
		DiscordPair.gPos = gPos; Iter1 = upper_bound(InversionSiteVec.begin(), InversionSiteVec.end(), DiscordPair, CompByDiscordPos);
		DiscordPair.gPos = gPos + FragmentSize; Iter2 = lower_bound(InversionSiteVec.begin(), InversionSiteVec.end(), DiscordPair, CompByDiscordPos);
		if (Iter1 == InversionSiteVec.end() || Iter2 == InversionSiteVec.end()) continue;
		vec.clear(); for (; Iter1 != Iter2; Iter1++) vec.push_back((Iter1->dist / 1000));
		sort(vec.begin(), vec.end()); vec.push_back(TwoGenomeSize);
		for (n = (int)vec.size(), Rscore = 0, score = j = 1; j < n; j++)
		{
			//printf("Rcan_%d: dist=%lld\n", j + 1, vec[j]);
			if (vec[j] - vec[j - 1] > 1)
			{
				if (score > Rscore) Rscore = score;
				score = 1;
			}
			else score++;
		}
		if (Rscore < cov_thr || Rscore < (int)(RCov*INV_TNL_ThrRatio)) continue;

		//printf("INV_can =%lld (Cov=%d vs %d): Lscore=%d Rscore=%d\n", (long long)gPos, LCov, RCov, Lscore, Rscore);
		if (Lscore > 0 && Rscore > 0)
		{
			INVnum++;
			Variant.gPos = gPos;
			Variant.NS = Lscore > Rscore ? Lscore : Rscore;
			Variant.VarType = var_INV;
			Variant.qscore = CalQualityScore(Variant.NS, cov_thr);
			VariantVec.push_back(Variant);
		}
	}
	if (INVnum > 0) inplace_merge(VariantVec.begin(), VariantVec.end() - INVnum, VariantVec.end(), CompByVarPos);
}

bool CheckNearbyVariant(int i, int num, int dist)
{
	bool bRet = false;
	if (i == 0)
	{
		if (VariantVec[i + 1].gPos - VariantVec[i].gPos <= dist) bRet = true;
	}
	else if (i == num - 1)
	{
		if (VariantVec[i].gPos - VariantVec[i - 1].gPos <= dist) bRet = true;
	}
	else
	{
		if ((VariantVec[i + 1].gPos - VariantVec[i].gPos) <= dist || (VariantVec[i].gPos - VariantVec[i - 1].gPos) <= dist) bRet = true;
	}
	return bRet;
}

bool CheckBadHaplotype(int i, int num, int dist)
{
	int j, diff;
	bool bRet = false;

	for (j = i + 1; j < num; j++)
	{
		if (VariantVec[j].gPos - VariantVec[i].gPos > dist) break;
		if (VariantVec[j].VarType == 0)
		{
			diff = abs(VariantVec[i].NS - VariantVec[j].NS);
			if (diff > 5 && (VariantVec[i].NS > VariantVec[j].NS ? VariantVec[i].NS >> 2 : VariantVec[j].NS >> 2)) bRet = true;
			break;
		}
	}
	for (j = i - 1; j >= 0; j--)
	{
		if (VariantVec[i].gPos - VariantVec[j].gPos > dist) break;
		if (VariantVec[j].VarType == 0)
		{
			diff = abs(VariantVec[i].NS - VariantVec[j].NS);
			if (diff > 10 && (VariantVec[i].NS > VariantVec[j].NS ? (int)(VariantVec[i].NS * 0.33) : (int)(VariantVec[j].NS * 0.33))) bRet = true;
			break;
		}
	}
	return bRet;
}

void ShowNeighboringProfile(int64_t gPos, Coordinate_t coor)
{
	int64_t i, p;

	for (i = -5; i <= 5; i++)
	{
		if (i == 0) printf("*");
		p = gPos + i;
		coor = DetermineCoordinate(p);
		printf("%s-%lld\t\t%d\t%d\t%d\t%d\tR=%d\tdepth=%d\n", ChromosomeVec[coor.ChromosomeIdx].name, (long long)coor.gPos, MappingRecordArr[p].A, MappingRecordArr[p].C, MappingRecordArr[p].G, MappingRecordArr[p].T, MappingRecordArr[p].multi_hit, GetProfileColumnSize(MappingRecordArr[p]));
	}
	printf("\n\n");
}

void GenVariantCallingFile()
{
	int i, num;
	int64_t gPos;
	float AlleleFreq;
	Coordinate_t coor;
	string filter_str;
	vector<int> VarNumVec(256);
	map<string, uint16_t>::iterator IndSeqMapIter;

	outFile = fopen(VcfFileName, "w"); ShowMetaInfo();

	//sort(VariantVec.begin(), VariantVec.end(), CompByVarPos);
	for (num = (int)VariantVec.size(), i = 0; i < num; i++)
	{
		gPos = VariantVec[i].gPos; coor = DetermineCoordinate(gPos);

		filter_str.clear();
		if (VariantVec[i].Filter.q10) filter_str += "q10;";
		if (bFilter && VariantVec[i].Filter.str_contraction) filter_str += "str_contraction;";
		if (bFilter && VariantVec[i].Filter.bad_haplotype) filter_str += "bad_haplotype;";
		if (filter_str == "") filter_str = "PASS"; else filter_str.resize(filter_str.length() - 1);

		if (VariantVec[i].VarType == var_SUB)
		{
			if (VariantVec[i].NS < 10 && CheckNearbyVariant(i, num, 10)) continue;
			VarNumVec[var_SUB]++;
			fprintf(outFile, "%s	%d	.	%c	%s	%d	%s	DP=%d;AD=%d;AF=%.3f;GT=%s;TYPE=SUBSTITUTE\n", ChromosomeVec[coor.ChromosomeIdx].name, (int)coor.gPos, RefSequence[VariantVec[i].gPos], VariantVec[i].ALTstr.c_str(), VariantVec[i].qscore, filter_str.c_str(), VariantVec[i].DP, VariantVec[i].NS, 1.0*VariantVec[i].NS / VariantVec[i].DP, (VariantVec[i].GenoType ? "0|1": "1|1"));
		}
		else if (VariantVec[i].VarType == var_INS)
		{
			if (VariantVec[i].NS < 5 && CheckNearbyVariant(i, num, 10)) continue;
			VarNumVec[var_INS]++; AlleleFreq = 1.0*VariantVec[i].NS / VariantVec[i].DP;
			fprintf(outFile, "%s	%d	.	%c	%c%s	%d	%s	DP=%d;AD=%d;AF=%.3f;GT=%s;TYPE=INSERT\n", ChromosomeVec[coor.ChromosomeIdx].name, (int)coor.gPos, RefSequence[gPos], RefSequence[gPos], VariantVec[i].ALTstr.c_str(), VariantVec[i].qscore, filter_str.c_str(), VariantVec[i].DP, VariantVec[i].NS, AlleleFreq, (VariantVec[i].GenoType ? "0|1" : "1|1"));
		}
		else if (VariantVec[i].VarType == var_DEL)
		{
			if (VariantVec[i].NS < 5 && CheckNearbyVariant(i, num, 10)) continue;
			VarNumVec[var_DEL]++; AlleleFreq = 1.0*VariantVec[i].NS / VariantVec[i].DP;
			fprintf(outFile, "%s	%d	.	%c%s	%c	%d	%s	DP=%d;AD=%d;AF=%.3f;GT=%s;TYPE=DELETE\n", ChromosomeVec[coor.ChromosomeIdx].name, (int)coor.gPos, RefSequence[gPos], VariantVec[i].ALTstr.c_str(), RefSequence[gPos], VariantVec[i].qscore, filter_str.c_str(), VariantVec[i].DP, VariantVec[i].NS, AlleleFreq, (VariantVec[i].GenoType ? "0|1" : "1|1"));
		}
		else if (VariantVec[i].VarType == var_TNL)
		{
			VarNumVec[var_TNL]++;
			fprintf(outFile, "%s	%d	.	%c	<TRANSLOCATION>	30	PASS	DP=%d;NS=%d;TYPE=BND\n", ChromosomeVec[coor.ChromosomeIdx].name, (int)coor.gPos, RefSequence[gPos - 1], VariantVec[i].DP, VariantVec[i].NS);
		}
		else if (VariantVec[i].VarType == var_INV)
		{
			VarNumVec[var_INV]++;
			fprintf(outFile, "%s	%d	.	%c	<INVERSION>	30	PASS	DP=%d;NS=%d;TYPE=BND\n", ChromosomeVec[coor.ChromosomeIdx].name, (int)coor.gPos, RefSequence[gPos - 1], VariantVec[i].DP, VariantVec[i].NS);
		}
	}
	std::fclose(outFile);
	fprintf(stderr, "\t%d(snp); %d(ins); %d(del); %d(trans); %d(inversion)\n", VarNumVec[var_SUB], VarNumVec[var_INS], VarNumVec[var_DEL], VarNumVec[var_TNL] >> 1, VarNumVec[var_INV] >> 1);
}

bool CheckNeighboringCoverage(int64_t gPos, int cov)
{
	int p, c, diff = 0;

	for (p = -5; p <= 5; p++)
	{
		if (p == 0) continue;
		c = GetProfileColumnSize(MappingRecordArr[gPos + p]);
		diff += abs(cov - c);
	}
	diff /= 10;
	if (diff >= 10 || (diff > 1 && diff >= (int)cov*0.1)) return false;
	else return true;
}

void *IdentifyVariants(void *arg)
{
	Variant_t Variant;
	int64_t gPos, end;
	unsigned char ref_base;
	string ins_str, del_str;
	vector<pair<char, int> > vec;
	vector<Variant_t> MyVariantVec;
	map<int64_t, map<string, uint16_t> >::iterator IndMapIter;
	int n, cov, cov_thr, freq_thr, ins_thr, del_thr, ins_freq, del_freq, tid = *((int*)arg);

	gPos = (tid == 0 ? 0 : (GenomeSize / iThreadNum)*tid);
	end = (tid == iThreadNum - 1 ? GenomeSize : (GenomeSize / iThreadNum)*(tid + 1));

	//map<int64_t, bool>::iterator it; map<int64_t, bool> obs_map;
	//if (bDebugMode) obs_map = LoadObservedPos();
	for (; gPos < end; gPos++)
	{
		if ((ref_base = nst_nt4_table[(unsigned short)RefSequence[gPos]]) != 4)
		{
			cov = GetProfileColumnSize(MappingRecordArr[gPos]);
			//if (bSomatic && (MappingRecordArr[gPos].multi_hit > (int)(cov*0.05))) continue;
			if ((cov_thr = BlockDepthArr[(int)(gPos / BlockSize)] >> 1) < MinAlleleFreq) cov_thr = MinAlleleFreq;
			if (bSomatic && cov_thr > MinAlleleFreq) cov_thr = MinAlleleFreq;

			if ((ins_thr = (int)(cov_thr*0.25)) < MinIndFreq) ins_thr = MinIndFreq;
			if ((del_thr = (int)(cov_thr*0.35)) < MinIndFreq) del_thr = MinIndFreq;
			ins_freq = GetAreaIndFrequency(gPos, InsertSeqMap, ins_str); del_freq = GetAreaIndFrequency(gPos, DeleteSeqMap, del_str);

			if (ins_freq >= ins_thr)
			{
				Variant.gPos = gPos; Variant.VarType = var_INS; Variant.DP = BlockDepthArr[(int)(gPos / BlockSize)]; Variant.NS = ins_freq;
				if (Variant.DP < Variant.NS) Variant.DP = Variant.NS; Variant.ALTstr = ins_str;
				if (Variant.NS >(int)(Variant.DP*0.8)) Variant.GenoType = 0; else Variant.GenoType = 1;
				if ((Variant.qscore = (int)(30.0 * ins_freq / Variant.DP)) > 30) Variant.qscore = 30;
				/*if (Variant.qscore >= MinVarConfScore)*/
				MyVariantVec.push_back(Variant);
			}
			if (del_freq >= del_thr)
			{
				Variant.gPos = gPos - 1; Variant.VarType = var_DEL; Variant.DP = BlockDepthArr[(int)(gPos / BlockSize)]; Variant.NS = del_freq;
				if (Variant.DP < Variant.NS) Variant.DP = Variant.NS; Variant.ALTstr = del_str;
				if (Variant.NS > (int)(Variant.DP*0.8)) Variant.GenoType = 0; else Variant.GenoType = 1;
				if ((Variant.qscore = (int)(30.0 * del_freq / Variant.DP)) > 30) Variant.qscore = 30;
				/*if (Variant.qscore >= MinVarConfScore)*/
				MyVariantVec.push_back(Variant);
			}
			//SUB
			if (cov >= cov_thr)
			{
				vec.clear(); freq_thr = (int)ceil(cov*(bSomatic ? 0.01 : FrequencyThr));
				if (freq_thr < MinAlleleFreq) freq_thr = MinAlleleFreq;

				if (ref_base != 0 && MappingRecordArr[gPos].A >= freq_thr) vec.push_back(make_pair('A', MappingRecordArr[gPos].A));
				if (ref_base != 1 && MappingRecordArr[gPos].C >= freq_thr) vec.push_back(make_pair('C', MappingRecordArr[gPos].C));
				if (ref_base != 2 && MappingRecordArr[gPos].G >= freq_thr) vec.push_back(make_pair('G', MappingRecordArr[gPos].G));
				if (ref_base != 3 && MappingRecordArr[gPos].T >= freq_thr) vec.push_back(make_pair('T', MappingRecordArr[gPos].T));
				
				if (vec.size() == 1)
				{
					Variant.gPos = gPos; Variant.VarType = var_SUB; Variant.DP = cov; Variant.NS = vec[0].second;
					if (vec[0].second >= (cov - freq_thr)) Variant.GenoType = 0; else Variant.GenoType = 1;
					//pthread_mutex_lock(&Lock); ShowVariationProfile(gPos - 5, gPos + 5); pthread_mutex_unlock(&Lock);
					Variant.ALTstr = vec[0].first;
					Variant.qscore = bSomatic ? (int)(30 * Variant.NS / (cov*0.05)) : (int)(10 * (1.0* Variant.NS / (cov*FrequencyThr)));
					if (Variant.qscore > 30) Variant.qscore = 30;

					MyVariantVec.push_back(Variant);
					
				}
				else if (vec.size() == 2 && CheckDiploidFrequency(cov, vec))
				{
					Variant.gPos = gPos; Variant.VarType = var_SUB; Variant.DP = cov; Variant.NS = vec[0].second + vec[1].second;
					Variant.GenoType = 1;
					//pthread_mutex_lock(&Lock);ShowVariationProfile(gPos - 5, gPos + 5); pthread_mutex_unlock(&Lock);
					Variant.ALTstr.resize(3); Variant.ALTstr[0] = vec[0].first; Variant.ALTstr[1] = ',';  Variant.ALTstr[1] = vec[1].first;
					Variant.qscore = bSomatic ? (int)(30 * Variant.NS / (cov*0.05)) : (int)(10 * (1.0* Variant.NS / (cov*FrequencyThr)));
					if (Variant.qscore > 30) Variant.qscore = 30;

					MyVariantVec.push_back(Variant);
				}
			}
		}
	}
	if ((n = (int)MyVariantVec.size()) > 0)
	{
		sort(MyVariantVec.begin(), MyVariantVec.end(), CompByVarPos);
		pthread_mutex_lock(&Lock);
		copy(MyVariantVec.begin(), MyVariantVec.end(), back_inserter(VariantVec)); inplace_merge(VariantVec.begin(), VariantVec.end() - n, VariantVec.end(), CompByVarPos);
		pthread_mutex_unlock(&Lock);
	}
	return (void*)(1);
}

void *VariantFiltering(void *arg)
{
	int64_t gPos;
	int i, num, tid = *((int*)arg);

	num = (int)VariantVec.size();
	for (i = tid; i < num; i += iThreadNum)
	{
		gPos = VariantVec[i].gPos;
		//VariantVec[i].Filter.bad_haplotype = VariantVec[i].Filter.clustered_event = VariantVec[i].Filter.str_contraction = false;
		VariantVec[i].Filter.q10 = VariantVec[i].qscore < 10 ? true : false;
		if (bFilter) VariantVec[i].Filter.str_contraction = MappingRecordArr[gPos].multi_hit > (int)(GetProfileColumnSize(MappingRecordArr[gPos])*0.05) ? true : false;
		//if (bFilter) VariantVec[i].Filter.clustered_event = CheckNearbyVariant(i, num, 100);
		if (bFilter) VariantVec[i].Filter.bad_haplotype = CheckBadHaplotype(i, num, 100);
	}
	return (void*)(1);
}

void VariantCalling()
{
	int i, *ThrIDarr;
	time_t t = time(NULL);
	pthread_t *ThreadArr = new pthread_t[iThreadNum];

	ThrIDarr = new int[iThreadNum];  for (i = 0; i < iThreadNum; i++) ThrIDarr[i] = i;

	if (ObserveBegPos != -1) printf("Profile[%lld-%lld]\n", (long long)ObserveBegPos, (long long)ObserveEndPos), ShowVariationProfile(ObserveBegPos, ObserveEndPos);
	BlockNum = (int)(GenomeSize / BlockSize); if (((int64_t)BlockNum * BlockSize) < GenomeSize) BlockNum += 1; 
	BlockDepthArr = new int[BlockNum]();
	for (i = 0; i < iThreadNum; i++) pthread_create(&ThreadArr[i], NULL, CalBlockReadDepth, &ThrIDarr[i]);
	for (i = 0; i < iThreadNum; i++) pthread_join(ThreadArr[i], NULL);
	fprintf(stderr, "Identify all variants...\n"); fflush(stderr);
	//iThreadNum = 1;
	for (i = 0; i < iThreadNum; i++) pthread_create(&ThreadArr[i], NULL, IdentifyVariants, &ThrIDarr[i]);
	for (i = 0; i < iThreadNum; i++) pthread_join(ThreadArr[i], NULL);
	// Apply filters
	for (i = 0; i < iThreadNum; i++) pthread_create(&ThreadArr[i], NULL, VariantFiltering, &ThrIDarr[i]);
	for (i = 0; i < iThreadNum; i++) pthread_join(ThreadArr[i], NULL);

	// Identify structural variants
	IdentifyBreakPointCandidates();
	if (BreakPointCanVec.size() > 0 && InversionSiteVec.size() > 0) IdentifyInversions();
	if (BreakPointCanVec.size() > 0 && TranslocationSiteVec.size() > 0) IdentifyTranslocations();
	fprintf(stderr, "\tWrite all the predicted sample variations to file [%s]...\n", VcfFileName); GenVariantCallingFile();

	fprintf(stderr, "variant calling has been done in %lld seconds.\n", (long long)(time(NULL) - t));
	delete[] ThrIDarr; delete[] ThreadArr; delete[] BlockDepthArr;
}
