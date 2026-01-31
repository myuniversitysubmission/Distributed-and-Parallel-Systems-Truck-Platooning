#include <CUnit/Basic.h>
#include "frame.h"




void write_speed(){

    unsigned int truck_id = 1;
    e_rw rw  = e_write;
    int param = 80; // assume speed is 80
    int value  = 100; // assume distance is 100
    EventType eventType = SPEED;

    char *message = constructMessage(truck_id, rw, param, value, eventType);
    CU_ASSERT_PTR_NOT_NULL_FATAL(message);

    DataFrame *frame = parseMessage(message);
    CU_ASSERT_PTR_NOT_NULL_FATAL(frame);

    CU_ASSERT_EQUAL(frame->truck_id, truck_id);
    CU_ASSERT_EQUAL(frame->readWriteFlag, rw);
    CU_ASSERT_EQUAL(frame->param, param);
    CU_ASSERT_EQUAL(frame->value, value);
    CU_ASSERT_EQUAL(frame->eventType, eventType);


}

void read_distance(){

    unsigned int truck_id = 2;
    e_rw rw  = e_read;
    int param = 80; // assume speed is 80
    int value  = 100; // assume distance is 100
    EventType eventType = DISTANCE;

    char *message = constructMessage(truck_id, rw, param, value, eventType);
    CU_ASSERT_PTR_NOT_NULL_FATAL(message);

    DataFrame *frame = parseMessage(message);
    CU_ASSERT_PTR_NOT_NULL_FATAL(frame);

    CU_ASSERT_EQUAL(frame->truck_id, truck_id);
    CU_ASSERT_EQUAL(frame->readWriteFlag, rw);
    CU_ASSERT_EQUAL(frame->param, param);
    CU_ASSERT_EQUAL(frame->value, value);
    CU_ASSERT_EQUAL(frame->eventType, eventType);


}

void ebrake_trigger(){

    unsigned int truck_id = 3;
    e_rw rw  = e_write;
    int param = 0; // brake
    int value  = 100; // assume distance is 100
    EventType eventType = EMERGENCY_BRAKE;

    char *message = constructMessage(truck_id, rw, param, value, eventType);
    CU_ASSERT_PTR_NOT_NULL_FATAL(message);

    DataFrame *frame = parseMessage(message);
    CU_ASSERT_PTR_NOT_NULL_FATAL(frame);

    CU_ASSERT_EQUAL(frame->eventType, eventType);


}



int main(void)
{
    if (CU_initialize_registry() != CUE_SUCCESS)
        return CU_get_error();

    CU_pSuite suite = CU_add_suite("Test Suite", NULL, NULL);

    CU_add_test(suite, "Write speed", write_speed);
    CU_add_test(suite, "Read distance", read_distance);
    CU_add_test(suite, "Emergency brake trigger", ebrake_trigger);

    CU_basic_set_mode(CU_BRM_VERBOSE);
    CU_basic_run_tests();

    CU_cleanup_registry();
    return 0;
}

