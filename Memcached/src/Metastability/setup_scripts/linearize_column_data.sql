use metastable_test_db;
SET @a:= 19741000;
UPDATE large_test_table SET tcol04=@a:=@a-1;