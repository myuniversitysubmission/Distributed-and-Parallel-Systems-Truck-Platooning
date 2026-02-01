#include <CUnit/Basic.h>
#include <winsock2.h>
#include "frame.h"
#include "queue.h"

typedef struct s_clientInfo {
    SOCKET *socClient;
    unsigned int client_id       : 3;
    unsigned int client_position : 3;
    TxQueue txQueue;
} clientInfo;

extern struct Truck truck;
extern int g_targetSpeed;
extern int g_targetDistance;
extern clientInfo *g_clients[];
extern void urgentBrakeAll();
extern void client_apply_emergency_brake(void);
extern void server_handle_intrusion(clientInfo *ci, DataFrame *frame);

//-------- VALIDATION TESTS --------


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

void ebrake_trigger()
{
    unsigned int truck_id = 3;
    e_rw rw_before  = e_write;
    int speed_before = 80;
    int dist_before  = 50;
    EventType event_before = SPEED;


    char *msg_speed = constructMessage(truck_id, rw_before, speed_before, dist_before, event_before);
    CU_ASSERT_PTR_NOT_NULL_FATAL(msg_speed);

    DataFrame *frame_speed = parseMessage(msg_speed);
    CU_ASSERT_PTR_NOT_NULL_FATAL(frame_speed);

    CU_ASSERT_EQUAL(frame_speed->eventType, SPEED);
    CU_ASSERT_EQUAL(frame_speed->param, speed_before);
    CU_ASSERT_EQUAL(frame_speed->value, dist_before);

    
    e_rw rw_eb  = e_write;
    int speed_after = 0;   
    int dist_after  = dist_before; 

    EventType event_eb = EMERGENCY_BRAKE;

    char *msg_eb = constructMessage(truck_id, rw_eb, speed_after, dist_after, event_eb);
    CU_ASSERT_PTR_NOT_NULL_FATAL(msg_eb);

    DataFrame *frame_eb = parseMessage(msg_eb);
    CU_ASSERT_PTR_NOT_NULL_FATAL(frame_eb);

    CU_ASSERT_EQUAL(frame_eb->truck_id, truck_id);
    CU_ASSERT_EQUAL(frame_eb->readWriteFlag, e_write);
    CU_ASSERT_EQUAL(frame_eb->eventType, EMERGENCY_BRAKE);
    CU_ASSERT_EQUAL(frame_eb->param, 0);          
    CU_ASSERT_EQUAL(frame_eb->value, dist_after);
}


void intrusion_trigger(void)
{
    
    truck.id             = 1;
    truck.currentSpeed   = 80;  // reported speed
    truck.currentDistance = 30; // reported distance

    g_targetSpeed    = 80;
    g_targetDistance = 30;

    
    clientInfo ci;
    SOCKET dummySock = INVALID_SOCKET;
    ci.socClient       = &dummySock;
    ci.client_id       = 1;
    ci.client_position = 0;
    TxQueue_init(&ci.txQueue);

    
    DataFrame intrusion;
    intrusion.truck_id      = truck.id;
    intrusion.readWriteFlag = e_write;
    intrusion.param         = truck.currentSpeed;    // reportedSpeed
    intrusion.value         = truck.currentDistance; // reportedDist
    intrusion.eventType     = INTRUSION;

    
    server_handle_intrusion(&ci, &intrusion);

   
    DataFrame cmd1, cmd2;
    bool ok1 = TxQueue_pop(&ci.txQueue, &cmd1);
    bool ok2 = TxQueue_pop(&ci.txQueue, &cmd2);

    CU_ASSERT_TRUE(ok1);
    CU_ASSERT_TRUE(ok2);

    CU_ASSERT_TRUE(
        (cmd1.eventType == SPEED && cmd2.eventType == DISTANCE) ||
        (cmd1.eventType == DISTANCE && cmd2.eventType == SPEED)
    );


    if (cmd1.eventType == SPEED) {
        g_targetSpeed = cmd1.value;
    } else if (cmd1.eventType == DISTANCE) {
        g_targetDistance = cmd1.value;
    }

    if (cmd2.eventType == SPEED) {
        g_targetSpeed = cmd2.value;
    } else if (cmd2.eventType == DISTANCE) {
        g_targetDistance = cmd2.value;
    }

    CU_ASSERT_EQUAL(g_targetSpeed, 60);
    CU_ASSERT_EQUAL(g_targetDistance, 80);
}


void queue_frame_test(void)
{
    TxQueue q;
    TxQueue_init(&q);

    //Queue should be empty at the beginning
    CU_ASSERT_EQUAL(q.count, 0);

    unsigned int truck_id = 7;
    e_rw rw  = e_write;
    int fake_speed = 180;
    int intr_code  = 2;
    EventType eventType = INTRUSION;

    
    char *msg = constructMessage(truck_id, rw, fake_speed, intr_code, eventType);
    CU_ASSERT_PTR_NOT_NULL_FATAL(msg);

    DataFrame *frame_in = parseMessage(msg);
    CU_ASSERT_PTR_NOT_NULL_FATAL(frame_in);

    //Push to queue
    bool ok = TxQueue_push(&q, frame_in);
    CU_ASSERT_TRUE(ok);
    CU_ASSERT_EQUAL(q.count, 1);

    //Pop from queue
    DataFrame out;
    memset(&out, 0, sizeof(DataFrame));

    ok = TxQueue_pop(&q, &out);
    CU_ASSERT_TRUE(ok);
    CU_ASSERT_EQUAL(q.count, 0);

    CU_ASSERT_EQUAL(out.truck_id,     frame_in->truck_id);
    CU_ASSERT_EQUAL(out.readWriteFlag, frame_in->readWriteFlag);
    CU_ASSERT_EQUAL(out.eventType,    frame_in->eventType);
    CU_ASSERT_EQUAL(out.param,        frame_in->param);
    CU_ASSERT_EQUAL(out.value,        frame_in->value);

    CU_ASSERT_EQUAL(out.eventType, INTRUSION);
    CU_ASSERT_EQUAL(out.param, fake_speed);
    CU_ASSERT_EQUAL(out.value, intr_code);
}


//-------- DEFECTED TESTS --------

void parse_message(){

    char malformed1[] = "1 1 80";      // param, value, eventType missing

    DataFrame *f1 = parseMessage(malformed1);
    CU_ASSERT_PTR_NULL(f1);

    // Text in speed
    char malformed2[] = "1 1 abc 100 1";

    DataFrame *f2 = parseMessage(malformed2);
    CU_ASSERT_PTR_NULL(f2);

    /* 42: invalid event type*/
    char invalid_event_msg[] = "1 1 80 100 42";

    DataFrame *f3 = parseMessage(invalid_event_msg);
    CU_ASSERT_PTR_NULL(f3);

}

void push_when_full()
{
    TxQueue q;
    TxQueue_init(&q);

    for (int i = 0; i < TX_QUEUE_SIZE; ++i) {
        DataFrame df = {0};
        df.truck_id = i + 1;
        df.readWriteFlag = e_write;
        df.param = 50;
        df.value = 100;
        df.eventType = SPEED;

        bool ok = TxQueue_push(&q, &df);
        CU_ASSERT_TRUE(ok);
    }

    DataFrame extra = {0};
    extra.truck_id = 999;
    extra.readWriteFlag = e_write;
    extra.param = 80;
    extra.value = 200;
    extra.eventType = SPEED;

    bool ok = TxQueue_push(&q, &extra);
    CU_ASSERT_FALSE(ok);

    CU_ASSERT_EQUAL(q.count, TX_QUEUE_SIZE);
}

void pop_from_empty()
{
    TxQueue q;
    TxQueue_init(&q);

    DataFrame out;
    memset(&out, 0xAA, sizeof(DataFrame));  

    bool ok = TxQueue_pop(&q, &out);
    CU_ASSERT_FALSE(ok);

}

void emergency_brake_resets_speed(void)
{
    truck.id            = 1;
    truck.currentSpeed  = 80;
    g_targetSpeed       = 80;

    client_apply_emergency_brake();

    CU_ASSERT_EQUAL(g_targetSpeed, 0);
    CU_ASSERT_EQUAL(truck.currentSpeed, 0);
}

void trigger_emergency(void)
{
    
    clientInfo ci;
    SOCKET dummySock = 0;
    ci.socClient      = &dummySock;
    ci.client_id      = 1;
    ci.client_position = 0;
    TxQueue_init(&ci.txQueue);

    
    DataFrame intrusion = {
        .truck_id      = 1,
        .readWriteFlag = e_write,
        .param         = 80,   // reportedSpeed
        .value         = 3,    // reportedDist (kritik)
        .eventType     = INTRUSION
    };

    server_handle_intrusion(&ci, &intrusion);

    DataFrame out1, out2;

    bool ok1 = TxQueue_pop(&ci.txQueue, &out1);
    bool ok2 = TxQueue_pop(&ci.txQueue, &out2);

    CU_ASSERT_TRUE(ok1);
    CU_ASSERT_TRUE(ok2);

    CU_ASSERT_TRUE(
        (out1.eventType == SPEED && out2.eventType == DISTANCE) ||
        (out1.eventType == DISTANCE && out2.eventType == SPEED)
    );

    if (out1.eventType == SPEED) {
        CU_ASSERT_EQUAL(out1.value, 60);
        CU_ASSERT_EQUAL(out2.value, 53);
    } else {
        CU_ASSERT_EQUAL(out2.value, 60);
        CU_ASSERT_EQUAL(out1.value, 53);
    }

}

//----- COMPONENT TEST-------

void frame_queue_roundtrip()
{
    TxQueue q;
    TxQueue_init(&q);

    DataFrame in;
    in.truck_id      = 1;
    in.readWriteFlag = e_write;
    in.param         = 80;
    in.value         = 30;
    in.eventType     = SPEED;

    bool pushed = TxQueue_push(&q, &in);
    CU_ASSERT_TRUE(pushed);
    CU_ASSERT_EQUAL(q.count, 1);

    DataFrame dequeued;
    bool popped = TxQueue_pop(&q, &dequeued);
    CU_ASSERT_TRUE(popped);
    CU_ASSERT_EQUAL(q.count, 0);

    char *msg = constructMessage(
        dequeued.truck_id,
        (e_rw)dequeued.readWriteFlag,
        dequeued.param,
        dequeued.value,
        dequeued.eventType
    );
    CU_ASSERT_PTR_NOT_NULL_FATAL(msg);

    DataFrame *parsed = parseMessage(msg);
    CU_ASSERT_PTR_NOT_NULL_FATAL(parsed);

    CU_ASSERT_EQUAL(parsed->truck_id,      in.truck_id);
    CU_ASSERT_EQUAL(parsed->readWriteFlag, in.readWriteFlag);
    CU_ASSERT_EQUAL(parsed->param,         in.param);
    CU_ASSERT_EQUAL(parsed->value,         in.value);
    CU_ASSERT_EQUAL(parsed->eventType,     in.eventType);

    free(msg);
    free(parsed);
}


void broadcasts_to_all_clients()
{
    clientInfo c1, c2;
    SOCKET s1 = INVALID_SOCKET;
    SOCKET s2 = INVALID_SOCKET;

    TxQueue_init(&c1.txQueue);
    TxQueue_init(&c2.txQueue);

    c1.socClient       = &s1;
    c1.client_id       = 1;
    c1.client_position = 0;

    c2.socClient       = &s2;
    c2.client_id       = 2;
    c2.client_position = 1;

    g_clients[0] = &c1;
    g_clients[1] = &c2;

    urgentBrakeAll();

    
    DataFrame out1, out2;
    bool ok1 = TxQueue_pop(&c1.txQueue, &out1);
    bool ok2 = TxQueue_pop(&c2.txQueue, &out2);

    CU_ASSERT_TRUE(ok1);
    CU_ASSERT_TRUE(ok2);

    CU_ASSERT_EQUAL(out1.eventType, EMERGENCY_BRAKE);
    CU_ASSERT_EQUAL(out2.eventType, EMERGENCY_BRAKE);

    
    CU_ASSERT_EQUAL(out1.truck_id, c1.client_id);
    CU_ASSERT_EQUAL(out2.truck_id, c2.client_id);

    
    g_clients[0] = NULL;
    g_clients[1] = NULL;
}

void intrusion_queue_commands()
{
    
    clientInfo ci;
    SOCKET dummySock = INVALID_SOCKET;
    ci.socClient       = &dummySock;
    ci.client_id       = 1;
    ci.client_position = 0;
    TxQueue_init(&ci.txQueue);

    DataFrame intrusion;
    intrusion.truck_id      = 1;
    intrusion.readWriteFlag = e_write;
    intrusion.param         = 80;  
    intrusion.value         = 30;  
    intrusion.eventType     = INTRUSION;

    server_handle_intrusion(&ci, &intrusion);

    
    DataFrame cmd1, cmd2;
    bool ok1 = TxQueue_pop(&ci.txQueue, &cmd1);
    bool ok2 = TxQueue_pop(&ci.txQueue, &cmd2);

    CU_ASSERT_TRUE(ok1);
    CU_ASSERT_TRUE(ok2);

    
    CU_ASSERT_TRUE(
        (cmd1.eventType == SPEED && cmd2.eventType == DISTANCE) ||
        (cmd1.eventType == DISTANCE && cmd2.eventType == SPEED)
    );

    int expectedSpeed = 60;
    int expectedDist  = 80; 

    if (cmd1.eventType == SPEED) {
        CU_ASSERT_EQUAL(cmd1.value, expectedSpeed);
        CU_ASSERT_EQUAL(cmd2.value, expectedDist);
    } else {
        CU_ASSERT_EQUAL(cmd2.value, expectedSpeed);
        CU_ASSERT_EQUAL(cmd1.value, expectedDist);
    }
}



int main(void)
{
    if (CU_initialize_registry() != CUE_SUCCESS)
        return CU_get_error();

    CU_pSuite val_suite = CU_add_suite("Validation Test Suite\n", NULL, NULL);
    CU_pSuite def_suite = CU_add_suite("Defect Test Suite\n", NULL, NULL);
    CU_pSuite comp_suite = CU_add_suite("Component Test Suite\n", NULL, NULL);

    //---- VALIDATION TEST SUITE----

    CU_add_test(val_suite, "Write speed", write_speed);
    CU_add_test(val_suite, "Read distance", read_distance);
    CU_add_test(val_suite, "Emergency brake trigger", ebrake_trigger);
    CU_add_test(val_suite, "Intrusion trigger", intrusion_trigger);
    CU_add_test(val_suite, "Queue frame test", queue_frame_test);

    printf("\n");

    //---- DEFECT TEST SUITE----

    CU_add_test(def_suite, "Message parsing & Invalid inputs", parse_message);
    CU_add_test(def_suite, "Push to queue when it is full ", push_when_full);
    CU_add_test(def_suite, "Pop from empty queue", pop_from_empty);
    CU_add_test(def_suite, "Speed reset", emergency_brake_resets_speed);
    CU_add_test(def_suite, "Emergency Trigger", trigger_emergency);

    //------ COMPONENT TEST SUITE-----
    CU_add_test(comp_suite, "Frame queue roundtrip", frame_queue_roundtrip);
    CU_add_test(comp_suite, "Intrusion queue commands", intrusion_queue_commands);
    CU_add_test(comp_suite, "Broadcasting to clients", broadcasts_to_all_clients);

    CU_basic_set_mode(CU_BRM_VERBOSE);
    CU_basic_run_tests();

    CU_cleanup_registry();
    return 0;
}

