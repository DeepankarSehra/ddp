#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <map>
#include <set>
#include "MinotaurConfig.h"
#include "Environment.h"
#include "Problem.h"
#include "Variable.h"
#include "Constraint.h"
#include "LinearFunction.h"
#include "Objective.h"
#include "Solution.h"
#include "Engine.h"
#include "IpoptEngine.h"
#include "FilterSQPEngine.h"
 
#include <cmath>               // Has INFINITY
#include <iostream>


using namespace Minotaur;
using namespace std;


vector<string> cc1 = {"MKPR", "MKPR UP", "MKPR DN", "SAKP", "DDSC", "DDSC DN", "DDSC SDG", "PVGW", "PBGW", "PBGW UP", "PBGW DN", "PVGW UP", "PVGW DN", "MKPD", "MKPD ", "SAKP 3RD"};
vector<string> cc2 = {"SVVR", "SVVR DN", "MUPR", "MUPR DN", "MUPR 4TH", "MUPR 3RD SDG", "KKDA DN", "KKDA UP", "IPE", "IPE 3RD", "VND", "MVPO", "MVPO DN", "NZM", "NIZM", "KKDA", "MUPR DN SDG"};

vector<string> crewControl = {"KKDA", "PVGW"};


// Utility function to convert hh:mm to minutes
int hhmm2mins(const string& hhmm) {
    int hrs = stoi(hhmm.substr(0, 2));
    int mins = stoi(hhmm.substr(3, 2));
    return hrs * 60 + mins;
}

// Utility function to convert minutes to hh:mm
string mins2hhmm(int mins) {
    int h = mins / 60;
    int m = mins % 60;
    string hh = (h < 10) ? "0" + to_string(h) : to_string(h);
    string mm = (m < 10) ? "0" + to_string(m) : to_string(m);
    return hh + ":" + mm;
}

// Service class to represent each service
class Service {
public:
    int servNum;
    string trainNum;
    string startStn;
    int startTime;
    string endStn;
    int endTime;
    string direction;
    int servDur;
    string jurisdiction;
    string stepbackTrainNum;
    bool servAdded;
    int breakDur;
    int tripDur;

    Service(vector<string>& attrs) {
        servNum = stoi(attrs[0]);
        trainNum = attrs[1];
        startStn = attrs[2];
        startTime = hhmm2mins(attrs[3]);
        endStn = attrs[4];
        endTime = hhmm2mins(attrs[5]);
        direction = attrs[6];
        servDur = stoi(attrs[7]);
        jurisdiction = attrs[8];
        stepbackTrainNum = attrs[9];
        servAdded = false;
        breakDur = 0;
        tripDur = 0;
    }
};

// Function to read the data from a CSV and return a list of services
vector<Service> fetchData(const string& filename) {
    vector<Service> servicesList;
    ifstream file(filename);
    string line;
    bool firstLine = true;

    while (getline(file, line)) {
        if (firstLine) {
            firstLine = false;
            continue;  // Skip the header
        }

        stringstream ss(line);
        string token;
        vector<string> row;

        while (getline(ss, token, ',')) {
            row.push_back(token);
        }

        servicesList.emplace_back(row);
    }

    file.close();
    return servicesList;
}

// Function to check if service can be appended
bool canAppend(vector<Service*>& duty, Service* service) {
    Service* lastService = duty.back();
    
    bool startEndStnTF = lastService->endStn == service->startStn;
    bool startEndTimeTF = 0 <= (service->startTime - lastService->endTime) && (service->startTime - lastService->endTime) <= 15;
    bool startEndStnTFafterBreak = lastService->endStn.substr(0, 4) == service->startStn.substr(0, 4);
    bool startEndTimeWithin = (service->startTime - lastService->endTime) >= 50 && (service->startTime - lastService->endTime) <= 150;

    bool startEndRakeTF = false;
    if (lastService->stepbackTrainNum == "No StepBack") {
        startEndRakeTF = lastService->trainNum == service->trainNum;
    } else {
        startEndRakeTF = lastService->stepbackTrainNum == service->trainNum;
    }

    if (startEndRakeTF) {
        if (startEndStnTF && startEndTimeTF) {
            int timeDur = service->endTime - duty.front()->startTime;
            string trainNum = service->trainNum;
            int contTimeDur = 0;

            for (auto it = duty.rbegin(); it != duty.rend(); ++it) {
                if ((*it)->stepbackTrainNum == "No StepBack") {
                    if ((*it)->trainNum == trainNum) {
                        contTimeDur = service->endTime - (*it)->startTime;
                        break;
                    }
                } else if ((*it)->stepbackTrainNum == trainNum) {
                    contTimeDur = service->endTime - (*it)->startTime;
                    trainNum = (*it)->trainNum;
                }
            }

            if (contTimeDur <= 180 && timeDur <= 445) {
                return true;
            }
        }
    } else if (startEndTimeWithin) {
        if (startEndStnTFafterBreak) {
            int timeDur = service->endTime - duty.front()->startTime;
            if (timeDur <= 445) {
                return true;
            }
        }
    }

    return false;
}

// Function to get initial feasible solution
vector<vector<int>> initialFeasibleSolution(vector<Service>& services) {
    vector<vector<int>> output;

    for (Service& service : services) {
        if (!service.servAdded) {
            vector<Service*> temp;
            temp.push_back(&service);
            service.servAdded = true;

            vector<Service*> unAddedServices;
            for (Service& serv : services) {
                if (!serv.servAdded) {
                    unAddedServices.push_back(&serv);
                }
            }

            // Sort services by startTime (dereferencing pointers)
            sort(unAddedServices.begin(), unAddedServices.end(), [](const Service* a, const Service* b) {
                return a->startTime < b->startTime;
            });

            for (Service* serv : unAddedServices) {
                if (canAppend(temp, serv)) {
                    temp.push_back(serv);
                    serv->servAdded = true;
                }
            }

            while (true) {
                bool validDuty = 
                    (find(cc1.begin(), cc1.end(), temp.front()->startStn) != cc1.end() && 
                     find(cc1.begin(), cc1.end(), temp.back()->endStn) != cc1.end()) ||
                    (find(cc2.begin(), cc2.end(), temp.front()->startStn) != cc2.end() && 
                     find(cc2.begin(), cc2.end(), temp.back()->endStn) != cc2.end()) ||
                    temp.size() == 1;

                if (validDuty) {
                    break;
                } else {
                    Service* removedServ = temp.back();
                    temp.pop_back();
                    removedServ->servAdded = false; // Reset the servAdded flag
                }
            }
            vector<int> srNum;
            for (const Service* serv : temp) {
                srNum.push_back(serv->servNum);
            }
            output.push_back(srNum);  
            
        }
    }
    return output;
}

// Modified printRoster function to accept vector<vector<Service*>>
void printRoster(const vector<vector<Service*>> &duties, const string &outputFile) {
    int sameCC = 0;
    ofstream file(outputFile);

    // Write the header
    file << "Duty No,Sign On Time,Sign On Loc,Sign Off Loc,Sign Off Time,Driving Hrs,Duty Hrs,Rake Num,Start Stn,Start Time,End Stn,End Time,Service Duration,Break\n";

    for (size_t index = 0; index < duties.size(); ++index) {
        const auto &servSet1 = duties[index];
        string dutyNo = std::to_string(index + 1);

        string signOnTime;
        if (find(crewControl.begin(), crewControl.end(), servSet1[0]->startStn.substr(0,4)) != crewControl.end()) {
            signOnTime = mins2hhmm(servSet1[0]->startTime - 15);
        } else {
            signOnTime = mins2hhmm(servSet1[0]->startTime - 25);
        }

        string signOnLoc = servSet1[0]->startStn;
        string signOffTime;
        if ( find(crewControl.begin(), crewControl.end(), servSet1.back()->endStn.substr(0,4)) != crewControl.end()) {
            signOffTime = mins2hhmm(servSet1.back()->endTime + 10);
        } else {
            signOffTime = mins2hhmm(servSet1.back()->endTime + 20);
        }

        string signOffLoc = servSet1.back()->endStn;
        int drivingDur = 0;
        for (auto& serv: servSet1)
        {
            drivingDur += serv->servDur;
        }

        string driveDur = mins2hhmm(drivingDur);
        string dutyDur = mins2hhmm(hhmm2mins(signOffTime) - hhmm2mins(signOnTime));
        
        bool validDuty = 
                    (find(cc1.begin(), cc1.end(), servSet1.front()->startStn) != cc1.end() && 
                     find(cc1.begin(), cc1.end(), servSet1.back()->endStn) != cc1.end()) ||
                    (find(cc2.begin(), cc2.end(), servSet1.front()->startStn) != cc2.end() && 
                     find(cc2.begin(), cc2.end(), servSet1.back()->endStn) != cc2.end());

        string sameJuris = validDuty ? "Yes" : "No";
        if (validDuty) {
            sameCC++;
        }

        vector<int> breaks;
        for (size_t i = 0; i < servSet1.size() - 1; ++i) {
            breaks.push_back(servSet1[i + 1]->startTime - servSet1[i]->endTime);
        }

        for (size_t i = 0; i < servSet1.size(); ++i) {
            const auto &service = servSet1[i];
            std::string newHeader;

            if (i < servSet1.size() - 1) {
                newHeader = service->trainNum + "," + service->startStn + "," + mins2hhmm(service->startTime) + "," +
                            service->endStn + "," + mins2hhmm(service->endTime) + "," + std::to_string(service->servDur) +
                            "," + std::to_string(breaks[i]) + "," + service->stepbackTrainNum;
            } else {
                newHeader = service->trainNum + "," + service->startStn + "," + mins2hhmm(service->startTime) + "," +
                            service->endStn + "," + mins2hhmm(service->endTime) + "," + std::to_string(service->servDur) +
                            ",0," + service->stepbackTrainNum;
            }

            if (i == 0) {
                file << dutyNo << "," << signOnTime << "," << signOnLoc << "," << signOffLoc << "," << signOffTime << "," 
                     << driveDur << "," << dutyDur << "," << sameJuris << "," << newHeader << "\n";
            } else {
                file << ",,,,," << newHeader << "\n";
            }
        }
        file << ",,,,,,,,,,,,,\n"; // Blank line between duties
    }

    cout << "same Juris % = " << (static_cast<float>(sameCC) / duties.size()) * 100 << "%" << std::endl;
}

// Function to check and add an edge between two services
bool nodeLegal(Service& service1, Service& service2) {
    // Check if service1 has no "StepBack"
    if (service1.stepbackTrainNum == "No StepBack") {
        // Case 1: Same train number
        if (service2.trainNum == service1.trainNum) {
            if (service1.endStn == service2.startStn && 0 <= (service2.startTime - service1.endTime) && (service2.startTime - service1.endTime) <= 15) {
                return true;
            }
        }
        // Case 2: Different train number, check station code match and time range
        else {
            if (service1.endStn.substr(0, 4) == service2.startStn.substr(0, 4) && service2.startTime >= service1.endTime + 30 && service2.startTime <= service1.endTime + 150) {
                return true;
            }
        }
    }
    // If service1 has a stepback train number
    else {
        // Case 3: Train matches stepback train number
        if (service2.trainNum == service1.stepbackTrainNum) {
            if (service1.endStn == service2.startStn && service1.endTime == service2.startTime) {
                return true;
            }
        }
        // Case 4: Stepback train, different train number, check station code match and time range
        else {
            if (service1.endStn.substr(0, 4) == service2.startStn.substr(0, 4) && service2.startTime >= service1.endTime + 30 && service2.startTime <= service1.endTime + 150) {
                return true;
            }
        }
    }
    return false;
}

//setting up the master problem
void columnGeneration(vector<Service>& services, vector<vector<int>>& duties, vector<vector<pair<int,Service*>>> &graph)
{
    map<int,vector<int>> dict;
    map<int,vector<int>> servicesInDuties;

    for(auto& serv : services)
    {
        servicesInDuties[serv.servNum] = {};
    }

    for(int i=0;i<duties.size();i++)
    {
        for(int j=0;j<duties[i].size();j++)
        {
            servicesInDuties[duties[i][j]].push_back(i);
        }
        dict[i] = duties[i];
    }

    EnvPtr env = (EnvPtr) new Environment();

    ProblemPtr p = (ProblemPtr) new Problem(env);

    //Define variables
    vector<VariablePtr> vars;
    for (auto& pair : dict) {
        vars.push_back(p->newVariable(0.0,1.0,Continuous,"x" + to_string(pair.first)));
    }

    //Define Objective
    LinearFunctionPtr lf; 
    lf = (LinearFunctionPtr) new LinearFunction();
    for (int i = 0; i < vars.size(); ++i) {
        lf->addTerm(vars[i], 1.0);
    }
    FunctionPtr f = new Function(lf);
    p->newObjective(f, 0.0, Minimize);

    //Define Constraints
    for (auto& pair : servicesInDuties) {
        LinearFunctionPtr constraint_lf = new LinearFunction();
        for (int j = 0; j < pair.second.size(); j++) {
            constraint_lf->addTerm(vars[pair.second[j]], 1); 
        }
        FunctionPtr cf = new Function(constraint_lf);
        p->newConstraint(cf, 1.0, 1.0);
    }
    
    //
    while(true)
    {
        //prepare to solve
        p->setNativeDer();
        p->prepareForSolve();
        p->write(cout);

        EnginePtr e = new FilterSQPEngine(env);
        e->setIterationLimit(4000);
        e->load(p);
        e->solve();
        cout<<e->getSolutionValue()<<"\n";
        cout<<e->getStatusString()<<"\n";
        break;
        
    }
    return ; 
}

// Main function to run the program
int main() {
    vector<Service> services = fetchData("inFiles/stepbackServices.csv");
    vector<vector<pair<int,Service*>>> graph(services.size());
    // Sort services by start time
    sort(services.begin(), services.end(), [](const Service& a, const Service& b) {
        return a.startTime < b.startTime;
    });

    for(int i=0;i<services.size();i++)
    {
        for(int j=i+1;j<services.size();j++)
        {
            if (nodeLegal(services[i],services[j]))
            {
                graph[services[i].servNum].push_back(make_pair(services[j].servNum, &services[j]));
            }
        }
    }

    for(auto& pair : graph[0])
    {
        cout<<pair.first<<endl;
    }
    return 0;

    // Get initial feasible solution
    auto duties = initialFeasibleSolution(services);
    columnGeneration(services, duties, graph);
    return 0;
}

