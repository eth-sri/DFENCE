//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// The class was added for DFENCE.
//
//===----------------------------------------------------------------------===//

#include "Params.h"

using std::cout;
using std::endl;
using std::string;
using namespace llvm;

double Params::flushProb = 1;
int Params::Property = PROP_NONE;
int Params::WMM = WMM_NONE;
int Params::Scheduler = RANDOM;
bool Params::logging = false;
set<string> Params::funcs_rec;
program_type Params::programToCheck;

void Params::processInputFile() {

	string methodsFile;
	string confFile;

	char *b = getenv(CONFDIR);
	if (b == NULL) {
		printf("The %s environment variable is not defined !\n", CONFDIR);
		exit(1);
	}
	string base(b);

	confFile = base;
        confFile += CONFILE;
	std::ifstream fin(confFile.c_str());
	if (!fin.is_open()) {
		cout << "Unable to open file " << confFile << endl;
		exit(1);
	}

	methodsFile = base;
	programToCheck = NO_PROGRAM;
	string str, tmpString;

	cout << "PARAMETERS OF THE EXECUTION" << endl;
	while (fin>>str) {
		if (str == "FLUSHPROB") {
			fin >> tmpString;
			fin >> tmpString;
			flushProb = atof(tmpString.c_str());
			cout << "Flush Probability: " << flushProb << endl;
		}
		else if (str == "WMM") {
			fin >> tmpString;
			fin >> tmpString;
			if(tmpString == "NONE") {
				WMM = WMM_NONE;
			}
			else if(tmpString == "TSO") {
				WMM = WMM_TSO; 
			}
			else if(tmpString == "PSO") {
				WMM = WMM_PSO;
			}
			else { 
				ASSERT(0, "Memory model not recognised");
			}
			cout << "Model: " << tmpString << endl;
		}
		else if (str == "PROPERTY") {
			fin >> tmpString;
			fin >> tmpString;
			if(tmpString == "LIN") {
				Property = PROP_LIN;
			}
			else if(tmpString == "SC") {
				Property = PROP_SC;
			}
			else {
				ASSERT(0, "Property not recognised");
			}
			cout << "Property: " << tmpString << endl;
		}
		else if (str == "PROGRAM") {
			fin >> tmpString;
			fin >> tmpString;
			if((tmpString == "WSQ_CHASE")) {
				programToCheck = WSQ_CHASE;
				methodsFile += WSQFILE;
			}
			else if(tmpString == "WSQ_LIFO") {
				programToCheck = WSQ_LIFO;
				methodsFile += WSQFILE;
			}
			else if(tmpString == "WSQ_FIFO") {
				programToCheck = WSQ_FIFO;
				methodsFile += WSQFILE;
			}
			else if (tmpString == "WSQ_THE") {
				programToCheck = WSQ_THE;
				methodsFile += WSQFILE;
			}
			else if (tmpString == "WSQ_ANCHOR") {
				programToCheck = WSQ_ANCHOR;
				methodsFile += WSQFILE;
			}
			else if (tmpString == "LF_MALLOC") {
				programToCheck = LF_MALLOC;
				methodsFile += MALLOCFILE;
			}
			else if (tmpString == "SKIP_LIST") {
				programToCheck = SKIP_LIST;
				methodsFile += SKIPFILE;
			}		
			else if (tmpString == "MS2" || tmpString == "MSN") {
				programToCheck = QUEUE;
				methodsFile += QUEUEFILE;
			}
		else if (tmpString == "SNARK") {
				programToCheck = DEQUE;
				methodsFile += DEQUEFILE;
			}
		else if (tmpString == "LAZYLIST" || tmpString == "HARRIS") {
				programToCheck = LINKSET;
				methodsFile += LINKSETFILE;
			}
			else {
				ASSERT(0, "Program not recognised");
			}
			cout << "Program : " << tmpString << endl;
		}
		else if (str == "LOG") {
			fin >> tmpString;
			fin >> tmpString;
			if (tmpString == "true") {
				logging = true;
				cout << "Shared read-write logging: yes" << endl;
			}
			else if (tmpString == "false") {
				logging = false;
				cout << "Shared read-write logging: no" << endl;
			}
			else {
				ASSERT(0, "Only true/false values recognised for logging option");
			}
		}
		else if (str == "SCHEDULER") {
			fin >> tmpString;
			fin >> tmpString;
			if (tmpString == "RANDOM") {
				Scheduler = RANDOM;
				cout << "Scheduler: RANDOM (empty buffers CAN be chosen for flushing)" << endl;
			}
			else {
				ASSERT(0, "The given type of scheduler cannot be recognized!");			
			}
		}
		else {
			ASSERT(0, "no such option to initialize ");
			printf("%s\n",str.c_str());
		}
	}

	if (Property == PROP_LIN || Property == PROP_SC) {

		string tmp;
		std::ifstream min;

		min.open(methodsFile.c_str(), std::ifstream::in);

		if (!min.is_open()) {
			cout << "Desired functions for recording the trace are needed to be mentioned in " << methodsFile << endl;
			exit(1);
		}

		while (min >> tmp) {
			funcs_rec.insert(tmp);
			cout << "Recording function " << tmp << endl;
		}
		min.close();
	}
	fin.close();
	cout << "END OF PARAMETERS OF EXECUTION" << endl;
}
