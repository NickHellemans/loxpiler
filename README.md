# Loxpiler

This repository is the result of going through the amazing book [Crafting Interpreters](https://craftinginterpreters.com/) by Robert Nystrom.
Following the book I implemented the Lox programming language as a bytecode virtual machine in C. The VM also comes with it's own mark & sweep garbage collector to take the trash out.

#### Fibonacci example
```js
fun fib(n) {
  if (n <= 1) return n;
  return fib(n - 2) + fib(n - 1);
}

for (var i = 0; i < 20; i = i + 1) {
  print fib(i);
}
```

## Features 

- Dynamic typing
- Automatic memory management through a garbage collector
- Datatypes:
	- Booleans
	- Numbers (double-precision floating point)
	- Strings
	- Nil (no value)
- Expressions:
	- Basic Arithmetic (+ - * /)
	- Comparison and equality (< /<= / > / >=)
	- Logical operators (! and or)
- Variables
- Control flow (if for & while)
- Functions
- Closures
- Classes
- Inheritance
- Built-in functions (STL) (print & clock)

## Syntax

### Booleans

```js
true;  // Not false.
false; // Not *not* false.
```

### Numbers

```js
1234;  // An integer.
12.34; // A decimal number.
```

### Strings 

```js
"I am a string";
"";    // The empty string.
"123"; // This is a string, not a number.
```

### Arithmetic operators

```js
add + me;
subtract - me;
multiply * me;
divide / me;

-negateMe;
```

### Comparison & equality
```js
less < than;
lessThan <= orEqual;
greater > than;
greaterThan >= orEqual;


1 == 2;         // false.
"cat" != "dog"; // true.

314 == "pi"; // false.
123 == "123"; // false.
```

### Logical operators

```js
!true;  // false.
!false; // true.
true and false; // false.
true and true;  // true.
false or false; // false.
true or false;  // true.
```

### Precedence

```js
var average = (min + max) / 2;
```

### Variables
```js
var imAVariable = "here is my value";
var iAmNil;

var breakfast = "bagels";
print breakfast; // "bagels".
breakfast = "beignets";
print breakfast; // "beignets".
```

### Control flow

```js
//If else
if (condition) {
  print "yes";
} else {
  print "no";
}

//While
var a = 1;
while (a < 10) {
  print a;
  a = a + 1;
}

//For
for (var a = 1; a < 10; a = a + 1) {
  print a;
}
```

### Functions

```js
makeBreakfast(bacon, eggs, toast);

fun printSum(a, b) {
  print a + b;
}

fun returnSum(a, b) {
  return a + b;
}

```

### Closures

```js
fun addPair(a, b) {
  return a + b;
}

fun identity(a) {
  return a;
}

print identity(addPair)(1, 2); // Prints "3".
```

---------------------------------------------
```js
//Declare local function inside another function
fun outerFunction() {
  fun localFunction() {
    print "I'm local!";
  }

  localFunction();
}
```

---------------------------------------------
```js
//Combine local functions, first-class functions, and block scope
fun returnFunction() {
  var outside = "outside";

  fun inner() {
    print outside;
  }

  return inner;
}

var fn = returnFunction();
fn();
```

### Classes
#### Declaration
```js
//Declare
class Breakfast {
  cook() {
    print "Eggs a-fryin'!";
  }

  serve(who) {
    print "Enjoy your breakfast, " + who + ".";
  }
}
// Store it in variables.
var someVariable = Breakfast;
// Pass it to functions.
someFunction(Breakfast);
```
#### Instances
```js
//Instantiation 
var breakfast = Breakfast();
print breakfast; // "Breakfast instance".

//Assigning to a field creates it if it doesn’t already exist.
breakfast.meat = "sausage";
breakfast.bread = "sourdough";
```
#### This 
```js
//Access field/method of current object with `this`
class Breakfast {
  serve(who) {
    print "Enjoy your " + this.meat + " and " +
        this.bread + ", " + who + ".";
  }

  // ...
}
```
#### Constructor
```js
//Constructor
class Breakfast {
  init(meat, bread) {
    this.meat = meat;
    this.bread = bread;
  }

  // ...
}

var baconAndToast = Breakfast("bacon", "toast");
baconAndToast.serve("Dear Reader");
// "Enjoy your bacon and toast, Dear Reader."
```

#### Inheritance
```js
//Inheritance
//Brunch is the derived class or subclass, and Breakfast is the base class or superclass.
class Brunch < Breakfast {
  drink() {
    print "How about a Bloody Mary?";
  }
}

var benedict = Brunch("ham", "English muffin");
benedict.serve("Noble Reader");
```

```js
//Call base class init with super
class Brunch < Breakfast {
  init(meat, bread, drink) {
    super.init(meat, bread);
    this.drink = drink;
  }
}
```
