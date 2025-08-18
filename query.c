/* For perf counters, we need to get a handle with PdhOpenQuery, so do the following sequence :

1. PdhOpenQuery
2. PdhAddCounter
3. PdhCollectQueryData
4. PdhGetFormattedCounterValue
5. PdhCloseQuery

The PdhOpenQuery function creates a query handle.
The PdhAddCounter function adds a counter to a query.
The PdhCollectQueryData function collects the data for a query.
The PdhGetFormattedCounterValue function retrieves the formatted counter value. 
The PdhCloseQuery function closes a query handle.
*/