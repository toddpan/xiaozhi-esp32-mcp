# Xiaozhi AI MCP Access Point Configuration Instructions (Official)

## Overview: A powerful interface for extending AI capabilities through remote control, computation, email operations, knowledge search, and more.

*MCP (Model Context Protocol)*: A protocol that allows servers to expose callable tools to language models. These tools enable models to interact with external systems, such as querying databases, calling APIs, or performing computations. Each tool is identified by a unique name and contains metadata describing its schema.

*MCP Access Point*: An interface used by voice terminals to connect local MCP services to Xiaozhi AI's large models.

*Obtaining the MCP Access Point*: Log in to the *xiaozhi.me* console, go to the agent's configuration role page, and you will see the agent's dedicated MCP access point in the lower right corner.

![Image](https://github.com/djairjr/xiaozhi-esp32-mcp/blob/alpha/docs/XiaoZhi1280X1280.PNG)

Sample code
GitHub: https://github.com/78/mcp-calculator

## MCP example
```
# server.py
from mcp.server.fastmcp import FastMCP
import logging
logger = logging.getLogger('test_mcp')

import math
import random

# Create an MCP server
mcp = FastMCP("Calculator")

#Add an addition tool
@mcp.tool()
def calculator(python_expression: str) -> dict: 
"""For mathematical calculation, always use this tool to calculate the result of a python expression. `math` and `random` are available.""" 
result = eval(python_expression) 
logger.info(f"Calculating formula: {python_expression}, result: {result}") 
return {"success": True, "result": result}

# Start the server
if __name__ == "__main__": `mcp.run(transport="stdio")`
```

## Important Notes⚠️
1. The names of tools and parameters in MCP must clearly indicate their function to the large model. Avoid abbreviations as much as possible, and provide comments explaining the tool's purpose and when to use it. For example, `calculator` tells the large model it's a calculator, and the parameter `python_expression` requires the large model to input a Python expression. If you're writing a `bing_search` tool, its parameter name should be `keywords`.

2. The documentation comments within the function (using quotation marks "...") guide the large model on when to use the tool, mentioning that functions from the `math` and `random` libraries can be used in expressions. These libraries have already been imported in the previous code.

3. Since the standard input and output of the MCP Server in this example project are used for data transmission, `print` cannot be used to print information; instead, a logger is used to output debugging information.

4. The return value of MCP is usually a string or JSON. In this example, the calculation result is returned in a JSON `result` field. The length of the return value is usually limited, similar to IoT commands on the device, typically limited to 1024 bytes.

5. There is an upper limit to the number of MCP toollist messages, which will be displayed later on the configuration page, calculated in tokens.

6. There is an upper limit to the number of connections per MCP access point.

## Operation effect
```
% export MCP_ENDPOINT=<your_mcp_endpoint>
% python mcp_pipe.py calculator.py
2025-05-16 09:07:09,009 - MCP_PIPE - INFO - Connecting to WebSocket server...
2025-05-16 09:07:09,096 - MCP_PIPE - INFO - Successfully connected to WebSocket server
2025-05-16 09:07:09,097 - MCP_PIPE - INFO - Started test.py process
Processing request of type ListToolsRequest
Processing request of type CallToolRequest
Calculating formula: 3.14159 * (8 / 2) ** 2, result: 50.26544
Processing request of type CallToolRequest
Calculating formula: random.randint(1, 100), result: 11
```

## Dialogue content

![Image](https://github.com/djairjr/xiaozhi-esp32-mcp/blob/alpha/docs/XiaoZhi_Dialog.png)
