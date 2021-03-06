--TEST--
Tarantool call commands test
--FILE--
<?php
require_once "lib/TarantoolUTest.php";

$tarantool = new Tarantool("localhost", 33013, 33015);
test_init($tarantool, 0);

echo "---------- test begin ----------\n";
echo "test call: myselect by primary index\n";
test_call($tarantool, "box.select", array("0", "0", 2), 0);
echo "----------- test end -----------\n\n";

echo "---------- test begin ----------\n";
echo "test call: call undefined function (expected error exception)\n";
test_call($tarantool, "fafagaga", array("fafa-gaga", "foo", "bar"), 0);
echo "----------- test end -----------\n\n";

echo "---------- test begin ----------\n";
echo "test call: sa_insert to key_1\n";
echo "sa_insert('1', 'key_1', '10')\n";
test_call($tarantool, "box.sa_insert", array("1", "key_1", "10"), 0);
echo "sa_insert('1', 'key_1', '11')\n";
test_call($tarantool, "box.sa_insert", array("1", "key_1", "11"), 0);
echo "sa_insert('1', 'key_1', '15')\n";
test_call($tarantool, "box.sa_insert", array("1", "key_1", "15"), 0);
echo "sa_insert('1', 'key_1', '101')\n";
test_call($tarantool, "box.sa_insert", array("1", "key_1", "101"), 0);
echo "sa_insert('1', 'key_1', '511')\n";
test_call($tarantool, "box.sa_insert", array("1", "key_1", "511"), 0);
echo "sa_insert('1', 'key_1', '16')\n";
test_call($tarantool, "box.sa_insert", array("1", "key_1", "16"), 0);
echo "sa_insert('1', 'key_1', '42')\n";
test_call($tarantool, "box.sa_insert", array("1", "key_1", "42"), 0);
echo "----------- test end -----------\n\n";

echo "---------- test begin ----------\n";
echo "test call: sa_select from key_1\n";
echo "sa_select('1', 'key_1', '101', '3')\n";
test_call($tarantool, "box.sa_select", array("1", "key_1", "101", "3"), 0);
echo "sa_select('1', 'key_1', '101', '2')\n";
test_call($tarantool, "box.sa_select", array("1", "key_1", "101", "2"), 0);
echo "sa_select('1', 'key_1', '511', '4')\n";
test_call($tarantool, "box.sa_select", array("1", "key_1", "511", "4"), 0);
echo "----------- test end -----------\n\n";

echo "---------- test begin ----------\n";
echo "test call: sa_delete from key_1\n";
echo "sa_delete('1', 'key_1', '11', '101', '511')\n";
test_call($tarantool, "box.sa_delete", array("1", "key_1", "11", "101", "511"), 0);
echo "----------- test end -----------\n\n";

echo "---------- test begin ----------\n";
echo "test call: sa_select from key_1\n";
echo "sa_select('1', 'key_1', '101', '3')\n";
test_call($tarantool, "box.sa_select", array("1", "key_1", "101", "3"), 0);
echo "sa_select('1', 'key_1', '101', '2')\n";
test_call($tarantool, "box.sa_select", array("1", "key_1", "101", "2"), 0);
echo "sa_select('1', 'key_1', '511', '4')\n";
test_call($tarantool, "box.sa_select", array("1", "key_1", "511", "4"), 0);
echo "----------- test end -----------\n\n";

echo "---------- test begin ----------\n";
echo "test call: sa_insert to key_2\n";
echo "sa_insert('1', 'key_2', '10')\n";
test_call($tarantool, "box.sa_insert", array("1", "key_2", "10"), 0);
echo "sa_insert('1', 'key_2', '8')\n";
test_call($tarantool, "box.sa_insert", array("1", "key_2", "8"), 0);
echo "sa_insert('1', 'key_2', '500')\n";
test_call($tarantool, "box.sa_insert", array("1", "key_2", "500"), 0);
echo "sa_insert('1', 'key_2', '166')\n";
test_call($tarantool, "box.sa_insert", array("1", "key_2", "166"), 0);
echo "sa_insert('1', 'key_2', '233')\n";
test_call($tarantool, "box.sa_insert", array("1", "key_2", "233"), 0);
echo "sa_insert('1', 'key_2', '357')\n";
test_call($tarantool, "box.sa_insert", array("1", "key_2", "357"), 0);
echo "----------- test end -----------\n\n";

echo "---------- test begin ----------\n";
echo "test call: sa_select from key_2\n";
echo "sa_select('1', 'key_2', '500', '100')\n";
test_call($tarantool, "box.sa_select", array("1", "key_2", "500", "100"), 0);
echo "sa_select('1', 'key_2', '18', '15')\n";
test_call($tarantool, "box.sa_select", array("1", "key_2", "18", "15"), 0);
echo "sa_select('1', 'key_2', '18', '1')\n";
test_call($tarantool, "box.sa_select", array("1", "key_2", "18", "1"), 0);
echo "----------- test end -----------\n\n";

echo "---------- test begin ----------\n";
echo "test call: sa_merge key_1 and key_2\n";
echo "sa_merge('1', 'key_1', 'key_2')\n";
test_call($tarantool, "box.sa_merge", array("1", "key_1", "key_2"), 0);
echo "----------- test end -----------\n\n";

echo "---------- test begin ----------\n";
echo "test call: sa_delete from key_1\n";
echo "sa_delete('1', 'key_1', '42')\n";
test_call($tarantool, "box.sa_delete", array("1", "key_1", "42"), 0);
echo "sa_delete('1', 'key_1', '16')\n";
test_call($tarantool, "box.sa_delete", array("1", "key_1", "16"), 0);
echo "sa_delete('1', 'key_1', '10')\n";
test_call($tarantool, "box.sa_delete", array("1", "key_1", "10"), 0);
echo "sa_delete('1', 'key_1', '15')\n";
test_call($tarantool, "box.sa_delete", array("1", "key_1", "15"), 0);
echo "----------- test end -----------\n\n";


test_clean($tarantool, 0);
?>
===DONE===
--EXPECT--
---------- test begin ----------
test call: myselect by primary index
result:
array(2) {
  ["count"]=>
  int(1)
  ["tuples_list"]=>
  array(1) {
    [0]=>
    array(6) {
      [0]=>
      int(2)
      [1]=>
      string(9) "Star Wars"
      [2]=>
      int(1983)
      [3]=>
      string(18) "Return of the Jedi"
      [4]=>
      string(460) "Luke Skywalker has returned
to his home planet of
Tatooine in an attempt
to rescue his friend
Han Solo from the
clutches of the vile
gangster Jabba the Hutt.

Little does Luke know
that the GALACTIC EMPIRE
has secretly begun construction
on a new armored space station
even more powerful than the
first dreaded Death Star.

When completed, this ultimate
weapon will spell certain
doom for the small band of
rebels struggling to restore
freedom to the galaxy..."
      [5]=>
      int(265041340203011)
    }
  }
}
----------- test end -----------

---------- test begin ----------
test call: call undefined function (expected error exception)
Exception: call failed: 12802(0x00003202): Procedure 'fafagaga' is not defined
----------- test end -----------

---------- test begin ----------
test call: sa_insert to key_1
sa_insert('1', 'key_1', '10')
result:
array(2) {
  ["count"]=>
  int(2)
  ["tuples_list"]=>
  array(2) {
    [0]=>
    array(1) {
      [0]=>
      string(5) "key_1"
    }
    [1]=>
    array(1) {
      [0]=>
      int(10)
    }
  }
}
sa_insert('1', 'key_1', '11')
result:
array(2) {
  ["count"]=>
  int(2)
  ["tuples_list"]=>
  array(2) {
    [0]=>
    array(1) {
      [0]=>
      string(5) "key_1"
    }
    [1]=>
    array(2) {
      [0]=>
      int(11)
      [1]=>
      int(10)
    }
  }
}
sa_insert('1', 'key_1', '15')
result:
array(2) {
  ["count"]=>
  int(2)
  ["tuples_list"]=>
  array(2) {
    [0]=>
    array(1) {
      [0]=>
      string(5) "key_1"
    }
    [1]=>
    array(3) {
      [0]=>
      int(15)
      [1]=>
      int(11)
      [2]=>
      int(10)
    }
  }
}
sa_insert('1', 'key_1', '101')
result:
array(2) {
  ["count"]=>
  int(2)
  ["tuples_list"]=>
  array(2) {
    [0]=>
    array(1) {
      [0]=>
      string(5) "key_1"
    }
    [1]=>
    array(4) {
      [0]=>
      int(101)
      [1]=>
      int(15)
      [2]=>
      int(11)
      [3]=>
      int(10)
    }
  }
}
sa_insert('1', 'key_1', '511')
result:
array(2) {
  ["count"]=>
  int(2)
  ["tuples_list"]=>
  array(2) {
    [0]=>
    array(1) {
      [0]=>
      string(5) "key_1"
    }
    [1]=>
    array(5) {
      [0]=>
      int(511)
      [1]=>
      int(101)
      [2]=>
      int(15)
      [3]=>
      int(11)
      [4]=>
      int(10)
    }
  }
}
sa_insert('1', 'key_1', '16')
result:
array(2) {
  ["count"]=>
  int(2)
  ["tuples_list"]=>
  array(2) {
    [0]=>
    array(1) {
      [0]=>
      string(5) "key_1"
    }
    [1]=>
    array(6) {
      [0]=>
      int(511)
      [1]=>
      int(101)
      [2]=>
      int(16)
      [3]=>
      int(15)
      [4]=>
      int(11)
      [5]=>
      int(10)
    }
  }
}
sa_insert('1', 'key_1', '42')
result:
array(2) {
  ["count"]=>
  int(2)
  ["tuples_list"]=>
  array(2) {
    [0]=>
    array(1) {
      [0]=>
      string(5) "key_1"
    }
    [1]=>
    array(7) {
      [0]=>
      int(511)
      [1]=>
      int(101)
      [2]=>
      int(42)
      [3]=>
      int(16)
      [4]=>
      int(15)
      [5]=>
      int(11)
      [6]=>
      int(10)
    }
  }
}
----------- test end -----------

---------- test begin ----------
test call: sa_select from key_1
sa_select('1', 'key_1', '101', '3')
result:
array(2) {
  ["count"]=>
  int(2)
  ["tuples_list"]=>
  array(2) {
    [0]=>
    array(1) {
      [0]=>
      string(5) "key_1"
    }
    [1]=>
    array(3) {
      [0]=>
      int(42)
      [1]=>
      int(16)
      [2]=>
      int(15)
    }
  }
}
sa_select('1', 'key_1', '101', '2')
result:
array(2) {
  ["count"]=>
  int(2)
  ["tuples_list"]=>
  array(2) {
    [0]=>
    array(1) {
      [0]=>
      string(5) "key_1"
    }
    [1]=>
    array(2) {
      [0]=>
      int(42)
      [1]=>
      int(16)
    }
  }
}
sa_select('1', 'key_1', '511', '4')
result:
array(2) {
  ["count"]=>
  int(2)
  ["tuples_list"]=>
  array(2) {
    [0]=>
    array(1) {
      [0]=>
      string(5) "key_1"
    }
    [1]=>
    array(4) {
      [0]=>
      int(101)
      [1]=>
      int(42)
      [2]=>
      int(16)
      [3]=>
      int(15)
    }
  }
}
----------- test end -----------

---------- test begin ----------
test call: sa_delete from key_1
sa_delete('1', 'key_1', '11', '101', '511')
result:
array(2) {
  ["count"]=>
  int(2)
  ["tuples_list"]=>
  array(2) {
    [0]=>
    array(1) {
      [0]=>
      string(5) "key_1"
    }
    [1]=>
    array(4) {
      [0]=>
      int(42)
      [1]=>
      int(16)
      [2]=>
      int(15)
      [3]=>
      int(10)
    }
  }
}
----------- test end -----------

---------- test begin ----------
test call: sa_select from key_1
sa_select('1', 'key_1', '101', '3')
result:
array(2) {
  ["count"]=>
  int(2)
  ["tuples_list"]=>
  array(2) {
    [0]=>
    array(1) {
      [0]=>
      string(5) "key_1"
    }
    [1]=>
    array(3) {
      [0]=>
      int(42)
      [1]=>
      int(16)
      [2]=>
      int(15)
    }
  }
}
sa_select('1', 'key_1', '101', '2')
result:
array(2) {
  ["count"]=>
  int(2)
  ["tuples_list"]=>
  array(2) {
    [0]=>
    array(1) {
      [0]=>
      string(5) "key_1"
    }
    [1]=>
    array(2) {
      [0]=>
      int(42)
      [1]=>
      int(16)
    }
  }
}
sa_select('1', 'key_1', '511', '4')
result:
array(2) {
  ["count"]=>
  int(2)
  ["tuples_list"]=>
  array(2) {
    [0]=>
    array(1) {
      [0]=>
      string(5) "key_1"
    }
    [1]=>
    array(4) {
      [0]=>
      int(42)
      [1]=>
      int(16)
      [2]=>
      int(15)
      [3]=>
      int(10)
    }
  }
}
----------- test end -----------

---------- test begin ----------
test call: sa_insert to key_2
sa_insert('1', 'key_2', '10')
result:
array(2) {
  ["count"]=>
  int(2)
  ["tuples_list"]=>
  array(2) {
    [0]=>
    array(1) {
      [0]=>
      string(5) "key_2"
    }
    [1]=>
    array(1) {
      [0]=>
      int(10)
    }
  }
}
sa_insert('1', 'key_2', '8')
result:
array(2) {
  ["count"]=>
  int(2)
  ["tuples_list"]=>
  array(2) {
    [0]=>
    array(1) {
      [0]=>
      string(5) "key_2"
    }
    [1]=>
    array(2) {
      [0]=>
      int(10)
      [1]=>
      int(8)
    }
  }
}
sa_insert('1', 'key_2', '500')
result:
array(2) {
  ["count"]=>
  int(2)
  ["tuples_list"]=>
  array(2) {
    [0]=>
    array(1) {
      [0]=>
      string(5) "key_2"
    }
    [1]=>
    array(3) {
      [0]=>
      int(500)
      [1]=>
      int(10)
      [2]=>
      int(8)
    }
  }
}
sa_insert('1', 'key_2', '166')
result:
array(2) {
  ["count"]=>
  int(2)
  ["tuples_list"]=>
  array(2) {
    [0]=>
    array(1) {
      [0]=>
      string(5) "key_2"
    }
    [1]=>
    array(4) {
      [0]=>
      int(500)
      [1]=>
      int(166)
      [2]=>
      int(10)
      [3]=>
      int(8)
    }
  }
}
sa_insert('1', 'key_2', '233')
result:
array(2) {
  ["count"]=>
  int(2)
  ["tuples_list"]=>
  array(2) {
    [0]=>
    array(1) {
      [0]=>
      string(5) "key_2"
    }
    [1]=>
    array(5) {
      [0]=>
      int(500)
      [1]=>
      int(233)
      [2]=>
      int(166)
      [3]=>
      int(10)
      [4]=>
      int(8)
    }
  }
}
sa_insert('1', 'key_2', '357')
result:
array(2) {
  ["count"]=>
  int(2)
  ["tuples_list"]=>
  array(2) {
    [0]=>
    array(1) {
      [0]=>
      string(5) "key_2"
    }
    [1]=>
    array(6) {
      [0]=>
      int(500)
      [1]=>
      int(357)
      [2]=>
      int(233)
      [3]=>
      int(166)
      [4]=>
      int(10)
      [5]=>
      int(8)
    }
  }
}
----------- test end -----------

---------- test begin ----------
test call: sa_select from key_2
sa_select('1', 'key_2', '500', '100')
result:
array(2) {
  ["count"]=>
  int(2)
  ["tuples_list"]=>
  array(2) {
    [0]=>
    array(1) {
      [0]=>
      string(5) "key_2"
    }
    [1]=>
    array(5) {
      [0]=>
      int(357)
      [1]=>
      int(233)
      [2]=>
      int(166)
      [3]=>
      int(10)
      [4]=>
      int(8)
    }
  }
}
sa_select('1', 'key_2', '18', '15')
result:
array(2) {
  ["count"]=>
  int(2)
  ["tuples_list"]=>
  array(2) {
    [0]=>
    array(1) {
      [0]=>
      string(5) "key_2"
    }
    [1]=>
    array(2) {
      [0]=>
      int(10)
      [1]=>
      int(8)
    }
  }
}
sa_select('1', 'key_2', '18', '1')
result:
array(2) {
  ["count"]=>
  int(2)
  ["tuples_list"]=>
  array(2) {
    [0]=>
    array(1) {
      [0]=>
      string(5) "key_2"
    }
    [1]=>
    array(1) {
      [0]=>
      int(10)
    }
  }
}
----------- test end -----------

---------- test begin ----------
test call: sa_merge key_1 and key_2
sa_merge('1', 'key_1', 'key_2')
result:
array(2) {
  ["count"]=>
  int(1)
  ["tuples_list"]=>
  array(1) {
    [0]=>
    array(10) {
      [0]=>
      int(500)
      [1]=>
      int(357)
      [2]=>
      int(233)
      [3]=>
      int(166)
      [4]=>
      int(42)
      [5]=>
      int(16)
      [6]=>
      int(15)
      [7]=>
      int(10)
      [8]=>
      int(10)
      [9]=>
      int(8)
    }
  }
}
----------- test end -----------

---------- test begin ----------
test call: sa_delete from key_1
sa_delete('1', 'key_1', '42')
result:
array(2) {
  ["count"]=>
  int(2)
  ["tuples_list"]=>
  array(2) {
    [0]=>
    array(1) {
      [0]=>
      string(5) "key_1"
    }
    [1]=>
    array(3) {
      [0]=>
      int(16)
      [1]=>
      int(15)
      [2]=>
      int(10)
    }
  }
}
sa_delete('1', 'key_1', '16')
result:
array(2) {
  ["count"]=>
  int(2)
  ["tuples_list"]=>
  array(2) {
    [0]=>
    array(1) {
      [0]=>
      string(5) "key_1"
    }
    [1]=>
    array(2) {
      [0]=>
      int(15)
      [1]=>
      int(10)
    }
  }
}
sa_delete('1', 'key_1', '10')
result:
array(2) {
  ["count"]=>
  int(2)
  ["tuples_list"]=>
  array(2) {
    [0]=>
    array(1) {
      [0]=>
      string(5) "key_1"
    }
    [1]=>
    array(1) {
      [0]=>
      int(15)
    }
  }
}
sa_delete('1', 'key_1', '15')
result:
array(2) {
  ["count"]=>
  int(2)
  ["tuples_list"]=>
  array(2) {
    [0]=>
    array(1) {
      [0]=>
      string(5) "key_1"
    }
    [1]=>
    array(0) {
    }
  }
}
----------- test end -----------

===DONE===
