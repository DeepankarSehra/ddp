import pandas as pd
import networkx as nx
import numpy as np
from scipy.optimize import linprog

# Sample dataset with more duties and Serial Number
data = {
    'Serial Number': [1, 2, 3, 4, 5, 6, 7, 8],
    'Rake Num': [101, 102, 103, 104, 105, 106, 107, 108],
    'Start Station': ['A', 'B', 'C', 'A', 'D', 'B', 'C', 'D'],
    'Start Time': ['08:00', '10:00', '12:00', '14:00', '09:00', '11:00', '13:00', '15:00'],
    'End Station': ['B', 'C', 'D', 'B', 'B', 'C', 'D', 'B'],
    'End Time': ['09:00', '11:00', '13:00', '15:00', '10:00', '12:00', '14:00', '16:00'],
    'Direction': ['Up', 'Up', 'Down', 'Down', 'Up', 'Up', 'Down', 'Down'],
    'service time': [60, 60, 60, 60, 60, 60, 60, 60],
    'Category': ['Express', 'Local', 'Express', 'Local', 'Express', 'Local', 'Express', 'Local']
}

# Convert to DataFrame
df = pd.DataFrame(data)

# Convert time columns to datetime for comparison
df['Start Time'] = pd.to_datetime(df['Start Time'], format='%H:%M')
df['End Time'] = pd.to_datetime(df['End Time'], format='%H:%M')

# Build the network with serial numbers included
def build_network(df):
    G = nx.DiGraph()
    # Add nodes with serial numbers
    for idx, row in df.iterrows():
        duty_id = row['Serial Number']
        G.add_node(duty_id, 
                   start_station=row['Start Station'], 
                   start_time=row['Start Time'], 
                   end_station=row['End Station'], 
                   end_time=row['End Time'], 
                   direction=row['Direction'], 
                   service_time=row['service time'], 
                   category=row['Category'])
    
    # Add edges
    for i, row1 in df.iterrows():
        for j, row2 in df.iterrows():
            if row1['End Station'] == row2['Start Station'] and row1['End Time'] < row2['Start Time']:
                G.add_edge(row1['Serial Number'], row2['Serial Number'], 
                           transition_time=(row2['Start Time'] - row1['End Time']).seconds // 60)
    return G

# Step 1: Initialize the RMP with a basic feasible solution (just individual duties)
def initialize_rmp(df):
    num_duties = len(df)
    # Basic initial columns (one column per duty, which corresponds to individual assignments)
    A = np.identity(num_duties)  # Coefficients for each duty in the initial solution
    cost = np.array([1] * num_duties)  # Let's assume each duty costs 1 to assign
    b = np.array([1] * num_duties)  # Each duty must be covered by exactly one schedule
    return A, cost, b

# Step 2: Solve the RMP (Linear Program) using `linprog`
def solve_rmp(A, cost, b):
    # Objective: minimize cost @ x, subject to Ax = b and x >= 0
    bounds = [(0, None)] * len(cost)  # Non-negativity constraint for each variable
    res = linprog(c=cost, A_eq=A, b_eq=b, bounds=bounds, method='highs')
    
    if res.success:
        # The shadow prices (dual variables) are available in the `res.con` attribute for equality constraints
        dual_values = res.get('eqlin').get('marginals')  # Dual values from linprog (shadow prices)
        return res.x, res.fun, dual_values, res
    else:
        print("RMP did not converge.")
        return None, None, None, None

# Step 3: Solve the pricing problem as a shortest path with dual values (reduced cost optimization)
def solve_pricing_problem(G, dual_values):
    shortest_path, min_cost = None, float('inf')
    
    # Create a new graph with edges weighted by node dual values
    G_weighted = nx.DiGraph()

    # Add the nodes and copy the dual values over
    for node in G.nodes():
        G_weighted.add_node(node, dual_value=dual_values[node - 1])  # serial number starts at 1

    # Add edges with weights based on the sum of dual values of connected nodes
    for u, v in G.edges():
        dual_u = G_weighted.nodes[u]['dual_value']
        dual_v = G_weighted.nodes[v]['dual_value']
        G_weighted.add_edge(u, v, weight=dual_u + dual_v)

    # Iterate over all nodes to find shortest paths
    for source in G_weighted.nodes:
        for target in G_weighted.nodes:
            if source != target:
                try:
                    path = nx.shortest_path(G_weighted, source=source, target=target, weight='weight')
                    cost = nx.path_weight(G_weighted, path, weight='weight')
                    if cost < min_cost:
                        shortest_path, min_cost = path, cost
                except nx.NetworkXNoPath:
                    continue
    
    return shortest_path, min_cost

# Step 4: Column generation process
def column_generation(df):
    # Build duty network
    G = build_network(df)
    
    # Initialize RMP
    A, cost, b = initialize_rmp(df)
    
    # Start the column generation process
    iteration = 0
    while True:
        print(f"\n=== Iteration {iteration} ===")
        # Step 1: Solve the RMP
        solution, objective_value, dual_values, _ = solve_rmp(A, cost, b)
        print(f"Current objective value: {objective_value}")
        print(f"Dual values: {dual_values}")
        
        # Step 2: Solve the Pricing Problem
        shortest_path, min_cost = solve_pricing_problem(G, dual_values)
        if min_cost >= 0:
            # No negative reduced cost columns found, stop column generation
            print("No more columns with negative reduced cost.")
            print(shortest_path)
            break
        
        print(f"New column found with path: {shortest_path} and reduced cost: {min_cost}")
        
        # Step 3: Add new column to the RMP
        new_column = np.zeros(len(df))
        for duty in shortest_path:
            new_column[duty - 1] = 1  # serial number starts at 1
        
        # Update A, cost (just append the new column)
        A = np.column_stack([A, new_column])
        cost = np.append(cost, 1)  # assume the cost of the new schedule is 1
        
        iteration += 1

# Run the column generation process
column_generation(df)
