use metastable_test_db;
SET @a:=  1504000;
UPDATE large_test_table SET tcol04=@a:=@a-1;