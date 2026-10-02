# Vatic Order Match Assignment

## Overview

A matching engine processes order requests to facilitate trading on an exchange, matching buy and sell orders to execute trades. Your task is to develop an application that processes a sequence of order requests (add or remove), executes matching logic, and outputs messages detailing resulting trades and order state changes.

The Logic section explains how orders should be matched. The Messages section defines the application's input and output formats. The Examples section provides sample input-output sequences, while the Problem section outlines application requirements.

## Logic

Orders represent offers to buy or sell a specified quantity at a certain price. Typically, buy orders aim for lower prices, while sell orders target higher prices. When a buy order matches or exceeds a sell order's price, a trade occurs for the smaller quantity of the two orders.

An incoming order causing prices to cross is an *aggressive order*, while existing orders in the book are *resting orders*. Resting orders are prioritized by price, with better prices for the aggressive order (lowest sell or highest buy) matched first. If multiple resting orders share the best price, the oldest order takes precedence.

Trades occur at the resting order's price and involve the smaller of the two quantities. If an aggressive order isn't fully filled, it continues matching with other resting orders in price-then-age order until fully filled or unmatched. Any remaining quantity becomes a new resting order in the book.

For example, if the resting orders are:

```
price  side  orders (oldest to newest, B = buy, S = sell)
1075   S     1       // one order to sell up to 1 at 1075
1050   S     10      // one order to sell up to 10 at 1050
1025   S     2, 5    // one order to sell up to 2 at 1025,
                     // and second newer order to sell up to 5
1000   B     9, 1    // buy up to 9 at 1000, second to buy up to 1
975    B     30      // buy up to 30 at 975
```

The best buy order is priced at 1000, and the best sell order is at 1025. Since no buyer is willing to pay enough to match a seller's price, and no seller is willing to lower their price, no match occurs.

If a new buy order for 3 lots (shares) at a price of 1050 arrives, a match will occur. The sell orders at or below 1050 are S10, S2, and S5. Since S2 and S5 offer better prices, they are matched first.

The S2 order, being older, is matched first for its full quantity, producing a trade of 2 units at 1025. The remaining 1 unit from the new buy order matches with S5, resulting in a trade of 1 unit at 1025, reducing S5 to S4.

This process generates two trade messages: one for a trade of 2 units at 1025 and another for 1 unit at 1025. Additionally, two order-related messages are produced: one to remove S2 and another to update S5 to S4. The updated list of standing orders will be:

```
price  side  orders
1075   S     1
1050   S     10
1025   S     4

1000   B     9, 1
975    B     30
```

If a new sell order arrives at a price of 1025, it will be added behind the S4 in the queue. While only a few price levels are shown here, remember that buy and sell orders can be placed at any price level.

## Messages

There are five types of messages involved in this problem, two messages your application shall accept as input and three that it will produce as output. The message types are identified by integer IDs:

| ID | Message              | Direction |
|----|----------------------|-----------|
| 0  | AddOrderRequest      | input     |
| 1  | CancelOrderRequest   | input     |
| 2  | TradeEvent           | output    |
| 3  | OrderFullyFilled     | output    |
| 4  | OrderPartiallyFilled | output    |

### Input

There are two input message types (requests).

**AddOrderRequest**: `msgtype, orderid, side, quantity, price` (e.g., `0,123,0,9,1000`)

- `msgtype`: 0
- `orderid`: unique positive integer to identify each order; used to reference existing orders for cancel and fill messages
- `side`: 0 (Buy), 1 (Sell)
- `quantity`: maximum quantity to buy/sell (positive integer)
- `price`: max price at which to buy/min price to sell (decimal number)

**CancelOrderRequest**: `msgtype, orderid` (e.g., `1,123`)

- `msgtype`: 1
- `orderid`: ID of the order to remove

### Output

**TradeEvent**: `msgtype, quantity, price` (e.g., `2,2,1025`)

- `msgtype`: 2
- `quantity`: amount that traded (positive integer)
- `price`: indicating the price at which the trade happened (decimal number)

Every pair of orders that matches generates a TradeEvent. If an aggressive order has enough quantity to match multiple resting orders, a TradeEvent is output for each match.

**OrderFullyFilled**: `msgtype, orderid` (e.g., `3,123`)

- `msgtype`: 3
- `orderid`: ID of the order that was removed

Any order fully filled by a trade is removed from the book and no longer available for future matches. When this occurs, an *OrderFullyFilled* message is generated. Each *TradeEvent* should produce at least one *OrderFullyFilled* message.

**OrderPartiallyFilled**: `msgtype, orderid, quantity` (e.g., `4,123,3`)

- `msgtype`: 4
- `orderid`: ID of the order to modify
- `quantity`: the new quantity of the modified order (positive integer)

Any order that has quantity remaining after a trade is only partially filled and the remaining quantity is available for matching against other orders in the future. When that happens, an OrderPartiallyFilled message is output. Each TradeEvent should result in at most one OrderPartiallyFilled message.

## Assignment

Construct an in-memory representation of the current order book from a sequence of order messages. You must generate your own dataset for testing, with messages read from stdin.

When an aggressive order matches resting orders, write the resulting trade messages and order updates (removals or modifications) to stdout. If multiple trades occur, output them in the order the resting orders were matched. For each pair of matched orders, output the following messages in sequence:

1. *TradeEvent*
2. Partial or fully filled message for the aggressive order
3. Partial or fully filled message for the resting order

Please use C++14 or higher to implement this assignment for the Linux platform. AI-assisted coding is permitted. Your code should be clean, readable, efficient, robust, and well-structured and well-designed, employing appropriate abstractions and tests. It will be evaluated by a developer, not an automated test suite. Please avoid using third-party libraries in your code, but third-party testing frameworks are allowed. Include instructions for building and running your solution.

Ensure no input causes the application to crash; log errors clearly to stderr. Include all datasets and supporting code used for testing.

Additionally, provide a description of your solution's performance, particularly for:

- Determining whether an *AddOrderRequest* results in a match
- Removing filled orders from the order book
- Removing canceled orders (via *CancelOrderRequest*)

While we understand that this is a take-home assignment and do not expect full production-grade performance, your solution should aim for the best possible performance within reasonable time and scope constraints. Please prioritize efficiency and responsiveness across all request types, and explain any trade-offs you make. If you choose to favor certain request paths over others, clearly articulate your reasoning. Additionally, describe the specific enhancements—such as optimizations, memory allocations and architectural changes, or scalability measures—you would implement if this were production-level code.

## Example

This is a complete example of input and output corresponding to the example described in the Details section. Note comments are not part of the input.

### Input/Output

The following messages on stdin:

```
0,1000000,1,1,1075
0,1000001,0,9,1000
0,1000002,0,30,975
0,1000003,1,10,1050
0,1000004,0,10,950
BADMESSAGE              // An erroneous input
0,1000005,1,2,1025
0,1000006,0,1,1000
1,1000004               // remove order
0,1000007,1,5,1025      // Original standing order book from Details
0,1000008,0,3,1050      // Matches! Triggers trades
```

The final message should cause your program to produce the following output on stdout:

```
2,2,1025        // Trade for 2 at 1025
4,1000008,1     // order partially filled, remaining open quantity 1
3,1000005       // order fully filled
2,1,1025        // Trade for 1 at 1025
3,1000008       // order fully filled
4,1000007,4     // order quantity reduced for partial fill
```

When encountering erroneous input, your application should output an error message to stderr, such as:

```
Unknown message type: BADMESSAGE
```

(Feel free to customize the error message format as needed.)

The input messages through 1000007 build the book from the example. None of these order messages cause a match and they are added to the book.

The order 1000008 causes a matching engine to match and produce trades. In this case the inbound order is fully filled so it is never added to the book. One of the resting orders is fully filled and removed from the book and the other resting gets its quantity adjusted.
