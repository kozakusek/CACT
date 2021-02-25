# CACT

A simple implementation of [actor model](https://en.wikipedia.org/wiki/Actor_model) in C, allowing only one system to work at the same time.

### Example programs

```bash
n | ./silnia
```
Computes n! using the actor model in which actors calculate their part and then send the message with calculated data to the next actor.

```bash
./macierz
```
Runs a programme which accepts as input 2 integers a, b which represent dimensions of a matrix and after that *2ab* integers representing
consecutive values of matrix cells and times that simulate the time taken to compute certain values. For example:
```bash
2 3
4 1 2 1 7 1
10 20 30 40 50 60
```
Represents a matrix  
```
  _ _ _ _ _ _ _   
| 4  | 2  | 7  |  
| 10 | 30 | 50 |  
  _ _ _ _ _ _ _
  ```
The programme after 1 + 1 + 1 ms will print 13 (4+2+7), and after next 20 + 40 + 60 ms will print 90 (10 + 30 + 50).

This example calculates row sums of given matrix by using b actors that send messeges with prefix sums to each other.
