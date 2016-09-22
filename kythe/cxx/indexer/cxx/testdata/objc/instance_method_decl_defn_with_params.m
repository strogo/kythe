// Checks that Objective-C instance methods with parameters are declared and
// defined.

//- @Box defines/binding BoxIface
@interface Box

//- @"foo" defines/binding FooDecl
//- FooDecl.node/kind function
//- FooDecl.complete incomplete
//- FooDecl childof BoxIface
-(int) foo;

//- @"bar:(int)k" defines/binding BarDecl
//- BarDecl.node/kind function
//- BarDecl.complete incomplete
//- BarDecl childof BoxIface
-(int) bar:(int)k;

@end

//- @Box defines/binding BoxImpl
@implementation Box

//- @"foo " defines/binding FooDefn
//- FooDefn.node/kind function
//- FooDefn.complete definition
//- FooDefn childof BoxImpl
//- @"foo " completes/uniquely FooDecl
-(int) foo {
  return 8;
}

//- @"bar:(int)k " defines/binding BarDefn
//- BarDefn.node/kind function
//- BarDefn.complete definition
//- BarDefn childof BoxImpl
//- @"bar:(int)k " completes/uniquely BarDecl
-(int) bar:(int)k {
  return k * 2;
}
@end

//- FooDecl named FooName
//- FooDefn named FooName
//- BarDecl named BarName
//- BarDefn named BarName

int main(int argc, char **argv) {
  return 0;
}

