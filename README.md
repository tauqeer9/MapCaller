MapCaller: An efficient and versatile approach for short-read mapping and variant identification using high-throughput sequenced data.
===================

Developers: Dr. Hsin-Nan Lin and Dr. Wen-Lian Hsu. Institute of Information Science, Academia Sinica, Taiwan.

# Introduction
MapCaller aligns every NGS short read against a reference genome and collects all the alignment information to deduce sequence variants. MapCaller adopts the mapping algorithm of KART to perform read alignment. It maintains a position frequency matrix to keep track of every nucleotide��s frequency at each position in the reference genome, and it collects all insertion and deletion events which are found during the read mapping. Furthermore, MapCaller also learns all possible break points from discordant or partial read alignments. Finally, MapCaller identifies sequence variants based on all the above-mentioned information. The novelty of our algorithm derives from the integration of read mapping and the variation information gathering into a coherent system for genomic variant calling. Thus, the inputs to MapCaller is one or more NGS read file and an index file for the reference genome, and the output is a VCF file for the variant calling result.

For more information, please refer to our manuscript (https://www.biorxiv.org/content/10.1101/783605v2)

The benchmark data sets and the performance evaluation program could be found at http://bioapp.iis.sinica.edu.tw/~arith/MapCaller/

# Download

Please use the command 
  ```
  $ git clone https://github.com/hsinnan75/MapCaller.git
  ```
to download the package of MapCaller.

# Dependencies

To compile MapCaller, it requires libboost-all-dev, libbz2-dev, and liblzma-dev pre-installed in your system.

# Compiling
To compile MapCaller and the index tool, please just type 'make' to compile MapCaller and bwt_index. If the compilation or the program fails, please contact me (arith@iis.sinica.edu.tw), Thanks.

# Test
You may run 'run_test.sh' to test MapCaller with a toy example.

# Get updates
  ```
  $ ./MapCaller update
  ```
or
  ```
  $ git fetch
  $ git merge origin/master master
  $ make
  ```

# Instructions

  ```
  $ ./MapCaller [options]
  ```

# Usage

To index a reference genome, it requires the target genome file (in fasta format) and the prefix of the index files (including the directory path).

  ```
  $ ./bwt_index ref_file[ex.ecoli.fa] index_prefix[ex. Ecoli]
  ```
The above command is to index the genome file Ecoli.fa and store the index files begining with ecoli.

Please note that if you find bwt_index does not work in your computer system, you may use bwa (http://bio-bwa.sourceforge.net/) to build the index files.
  ```
  $ ./bwa index -p index_prefix xxxx.fa
  ```
To perform variant calling, MapCaller requires the the index files of the reference genome and at least one read file (two read files for the separated paired-end reads). Users should use -i to specify the prefix of the index files (including the directory path).

 case 1: standard vcf output / sam output (optional) / bam output (optional)
  ```
 $ ./MapCaller -i ecoli -f ReadFile1.fa -f2 ReadFile2.fa -vcf out.vcf [-sam out.sam][-bam out.bam]
  ```

 case 2: multiple input 
  ```
 $ ./MapCaller -i ecoli -f ReadFileA_1.fq ReadFileB_1.fq ReadFileC_1.fq -f2 ReadFileA_2.fq ReadFileB_2.fq ReadFileC_2.fq -vcf out.vcf
  ```

# File formats

- Reference genome files

    All reference genome files should be in FASTA format.

- Read files

    MapCaller is designed for NGS short reads. All reads files should be in FASTA/FASTQ format. Input files can be compressed with gzip format.
    Please note, if reads are stored in FASTA format, each read sequence is not allowed to be wrapped (split over multiple lines).
    Read sequences should be capital letters. The quality scores in FASTQ are not considered in the alignments. 
    If paired-end reads are separated into two files, use -f and -f2 to indicate the two filenames. The i-th reads in the two files are paired. If paired-end reads are in the same file, use -p. The first and second reads are paired, the third and fourth reads are paired, and so on. For the latter case, use -p to indicate the input file contains paired-end reads.

- Output file

    MapCaller outputs a SAM/BAM file [optional] and a VCF file. 

# Parameter setting

 ```
-t INT number of threads [16]

-i STR index prefix [BWT based (BWA), required]

-f STR read filename [required, fasta or fastq or fq.gz]

-f2 STR read filename2 [optional, fasta or fastq or fq.gz], f and f2 are files with paired reads

-p the input read file consists of interleaved paired-end sequences [false]

-sam STR SAM output [optional, default: no mapping output]

-bam STR BAM output [optional, default: no mapping output]

-vcf STR VCF output [output.vcf]

-filter Apply variant filters (under test) [false]

-no_vcf No VCF output [false]

-size Sequencing fragment size [default: 500, MapCaller can predict the fragment size automatically]

-ad INT Minimal ALT allele count [3]

-somatic detect somatic mutations [false]

-m output multiple alignments [false]

-v version number

-h help

  ```
# Changes
version 0.9.9.1: Adjust read depth threshold.
version 0.9.9.2: Remove variants which appear in repetitive regions.
version 0.9.9.3: Adjust read depth threshold for somatic mutation detection.
version 0.9.9.4: Add variant filters
version 0.9.9.5: Fix a bug on read count
version 0.9.9.6: Fix a bug on read count
version 0.9.9.7: Fix typos and warnings.

# Acknowledgements
We would like to thank Mr. Torsten Seemann for valuable comments.
