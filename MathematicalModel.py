import plotly.express as px
import plotly.graph_objects as go
import matplotlib.pyplot as plt
import pyomo.environ as pyo
from pyomo.core import ConcreteModel
from pyomo.opt import SolverFactory, SolverStatus, TerminationCondition
import math
from datetime import timedelta
import csv
import pandas as pd
import random

df = pd.read_csv("stepbackServices.csv")

services = [df.iloc[i,0] for i in range(len(df)) ]

service_assignments = {}

servicesInPath = {key: [] for key in services}

with open("SetOfDuties.csv", 'rb') as file:
    file_content = file.read()
    file_content = file_content.replace(b'\x00', b'')
    file_content = file_content.decode('utf-8')
    from io import StringIO
    file_like_object = StringIO(file_content)
    reader = csv.reader(file_like_object)
    for index, row in enumerate(reader):
        # Filter out NULL values (assuming NULL is represented as the string 'NULL')
        filtered_row = [int(value) for value in row if value != 'NULL' and value != '']

        if filtered_row:
            for ii in filtered_row:
                servicesInPath[ii].append(index)
                service_assignments[index] = filtered_row


# x = len(service_assignments)
# for i in services:
#     # randomServicesAssignments[x] = [i] 
#     service_assignments[x] = [i]
#     servicesInPath[i].append(x)
#     x += 1

num_services = len(services)
num_crew = len(service_assignments)
print(f'services:{num_services} duties/crew :{num_crew}')

for xyz in range(1): 

    servicesInDuties = []
    for index,services1 in service_assignments.items():
        for service in services1:
            if service not in servicesInDuties:
                servicesInDuties.append(service)
        if len(servicesInDuties) == num_services:
            break

    if num_services != len(servicesInDuties):
        print("Total No. of services are not appended in iteration: "," and ", num_services, len(servicesInDuties))
        continue
    
    print(service_assignments)

    # madhavModel = ConcreteModel()
    # madhavModel.fPath = pyo.Var([path for path in service_assignments.keys()], domain=pyo.Binary) #Binary or NonNegativeReals
    
    # def objectiveRule(model):
    #     minimumPath = sum(model.fPath[path] for path in service_assignments.keys())
    #     return minimumPath 

    # madhavModel.OBJ = pyo.Objective(rule=objectiveRule, sense=pyo.minimize)
    # madhavModel.ConsList = pyo.ConstraintList()
    
    # for edgeService,edgepaths in servicesInPath.items():
    #     madhavModel.ConsList.add(sum(madhavModel.fPath[pathId] for pathId in edgepaths) ==  1)
    # madhavModel.write('DMRC\DMRC\tmFiles\Model.nl', format ='nl' )