#ifndef _SYS_QUEUE_H_
#define _SYS_QUEUE_H_

// 20250323 1940: For Super Earth! - saitewasreset
// 20250323 1940: wxyglp - OHHHH

/*
 * 为何要使用do-while(0)？
 * 1、避免宏展开后的语法错误：如果宏定义中有多条语句，直接使用 {}
 包裹这些语句可能会导致语法错误。例如，如果在 if 语句中使用这样的宏，可能会导致
 else 语句无法正确匹配。do-while(0) 可以确保宏展开后仍然是一个完整的语句块。
 * 2、强制分号：使用 do-while(0)
 可以确保在宏调用后必须加上分号，这符合C语言的语法习惯。如果没有
 do-while(0)，宏调用后不加分号可能会导致编译错误或逻辑错误。
 * 3、局部变量作用域：如果宏定义中使用了局部变量，do-while(0)
 可以确保这些变量的作用域仅限于宏内部，避免与外部代码中的变量名冲突。
 */

/*
 * This file defines three types of data structures: lists, tail queues,
 * and circular queues.
 *
 * A list is headed by a single forward pointer(or an array of forward
 * pointers for a hash table header). The elements are doubly linked
 * so that an arbitrary element can be removed without a need to
 * traverse the list. New elements can be added to the list before
 * or after an existing element or at the head of the list. A list
 * may only be traversed in the forward direction.
 *
 * A tail queue is headed by a pair of pointers, one to the head of the
 * list and the other to the tail of the list. The elements are doubly
 * linked so that an arbitrary element can be removed without a need to
 * traverse the list. New elements can be added to the list before or
 * after an existing element, at the head of the list, or at the end of
 * the list. A tail queue may only be traversed in the forward direction.
 *
 * A circle queue is headed by a pair of pointers, one to the head of the
 * list and the other to the tail of the list. The elements are doubly
 * linked so that an arbitrary element can be removed without a need to
 * traverse the list. New elements can be added to the list before or after
 * an existing element, at the head of the list, or at the end of the list.
 * A circle queue may be traversed in either direction, but has a more
 * complex end of list detection.
 *
 * For details on the use of these macros, see the queue(3) manual page.
 */

/*
 * List declarations.
 */

/*
 * 链表由 LIST_HEAD 宏定义的结构体作为头部。
 * 该结构体包含一个指向链表中首个元素的指针。
 * 元素采用双向链接方式，使得无需遍历链表即可删除任意元素。
 * 新元素可被添加到某个已有元素之后，或直接插入链表头部。
 * LIST_HEAD 结构体的声明方式如下：
 *
 *       LIST_HEAD(HEADNAME, TYPE) head;
 *
 * 其中 HEADNAME 是要定义的结构体名称，TYPE 是要链接到链表中的元素类型。
 *
 * 例如：
 *
 * LIST_HEAD(Page_list, Page)
 *
 * 定义了一个名为Page_list的结构体作为链表头，链表的节点类型为Page
 * 注意Page需要单独定义，且包含一个通过LIST_ENTRY宏定义的字段以记录链接信息。
 */
#define LIST_HEAD(name, type)                                                  \
    struct name {                                                              \
        struct type *lh_first; /* first element */                             \
    }

/*
 * 将LIST_HEAD_INITIALIZER(head)赋值给链表头，可重置其为合法的空链表。
 */
#define LIST_HEAD_INITIALIZER(head) {NULL}

/*
 * Use this inside a structure "LIST_ENTRY(type) field" to use
 * x as the list piece.
 *
 * le_prev指向上一个节点中存储le_next的内存区域；
 * 通过使用le_prev，可以在不知道链表节点结构体的具体构造的情况下修改节点指向
 * 以完成链表操作
 */
#define LIST_ENTRY(type)                                                       \
    struct {                                                                   \
        struct type *le_next;  /* next element */                              \
        struct type **le_prev; /* address of previous next element */          \
    }

/*
 * List functions.
 */

/*
 * 返回名为`head`的链表是否为空。
 *
 * Precondition:
 * - `head` 必须是指向合法链表的指针（由LIST_INIT初始化）。
 */
#define LIST_EMPTY(head) ((head)->lh_first == NULL)

/*
 * 返回名为`head`的链表中的第一个元素。
 *
 * Precondition:
 * - `head` 必须是指向合法链表的指针（由LIST_INIT初始化）。
 *
 * Return：
 * - 链表的第一个节点，若链表为空，返回NULL
 */
#define LIST_FIRST(head) ((head)->lh_first)

/*
 * 遍历名为"head"的链表中的元素。
 * 在循环过程中，将链表元素赋值给变量"var"
 * 其中，"field"对应链表节点结构体中LIST_ENTRY类型的字段的名称。
 *
 * 使用方法：
 * LIST_FOREACH(var, head, field) {
 *    // operation...
 * }
 */
#define LIST_FOREACH(var, head, field)                                         \
    for ((var) = LIST_FIRST((head)); (var); (var) = LIST_NEXT((var), field))

/*
 * 设置/初始化名为"head"的链表头
 */
#define LIST_INIT(head)                                                        \
    do {                                                                       \
        LIST_FIRST((head)) = NULL;                                             \
    } while (0)

/*
 * 将元素'elm'插入到已存在于链表中的元素'listelm'**之后**。
 * 'field'名称即上述链接字段。
 *
 * listelm、elm均为**指针**
 * field是类型为`LIST_ENTRY(T)`的字段名称
 *
 * Precondition：
 * - `listelm`必须不为NULL且必须是链表中的有效节点（可以是最后一个节点）
 * - `elm`必须不为NULL且必须是未链接的节点
 *
 * 具体地，该宏执行如下4个操作：
 * 1.
 * 将elm节点的下个节点设置为listelm节点的下个节点（LIST_NEXT宏展开为le_next变量，是左值）
 * 2.
 * 将elm节点的前一个节点设置为listelm（注意，取前一个节点le_next字段的地址，使用LIST_NEXT宏）
 * 3.
 * （若listelm存在下一个节点），将“下一个节点”的前一个节点设置为elm（注意，取le_next字段的地址，使用LIST_NEXT宏）
 * 4. 将listelm的下一个节点设置为elm（使用LIST_NEXT宏）
 */
#define LIST_INSERT_AFTER(listelm, elm, field)                                 \
    do {                                                                       \
        LIST_NEXT((elm), field) = LIST_NEXT((listelm), field);                 \
        (elm)->field.le_prev = &LIST_NEXT((listelm), field);                   \
        if (LIST_NEXT((listelm), field) != NULL) {                             \
            LIST_NEXT((listelm), field)->field.le_prev =                       \
                &LIST_NEXT((elm), field);                                      \
        }                                                                      \
        LIST_NEXT((listelm), field) = (elm);                                   \
    } while (0)

/*
 * 将元素"elm"插入到已存在于链表中的元素"listelm"**之前**。
 * "field"名称即上述链接字段。
 *
 * listelm、elm均为**指针**
 * filed是类型为`LIST_ENTRY(T)`的字段名称
 *
 * Precondition：
 * - `listelm`必须不为NULL且必须是链表中的有效节点
 * - `elm`必须不为NULL且必须是未链接的节点
 * - `listelm`必须有一个有效的前驱节点（listelm->field.le_prev）必须不为NULL
 *    若要在链表的第一个元素前插入，请使用LIST_INSERT_HEAD宏
 *
 * 具体地，该宏执行如下4个操作：
 * 1.
 * 将elm节点的下个节点设置为listelm节点（LIST_NEXT宏展开为le_next变量，是左值）
 * 2.
 * 将elm节点的前一个节点设置为listelm节点的前一个节点（注意，取前一个节点le_next字段的地址，这恰好存储在(listelm)->field.le_prev中）
 * 3.
 * 将listelm节点的前一个节点的下一个节点设置为elm（注意，前一个节点le_next字段的地址，恰好存储在(listelm)->field.le_prev中）
 * 4. 将listelm节点的前一个节点设置为elm
 */
#define LIST_INSERT_BEFORE(listelm, elm, field)                                \
    do {                                                                       \
        LIST_NEXT((elm), field) = (listelm);                                   \
        (elm)->field.le_prev = (listelm)->field.le_prev;                       \
        *(listelm)->field.le_prev = (elm);                                     \
        (listelm)->field.le_prev = &LIST_NEXT((elm), field);                   \
    } while (0)

/*
 * 将元素"elm"插入到链表"head"的第一个元素。
 * "field"名称即上述链接字段。
 *
 * head、elm均为**指针**
 * filed是类型为`LIST_ENTRY(T)`的字段名称
 *
 * Precondition：
 * - `head`必须是指向合法链表的指针（由LIST_INIT初始化）。
 * - `elm`必须不为NULL且必须是未链接的节点
 * 注意：
 * - 插入后，elm节点的le_prev将指向链表头中lh_first变量的内存空间
 *
 * 具体地，该宏执行如下4个操作：
 * 1.
 * 将elm节点的下个节点设置为原链表的第一个节点（使用LIST_FIRST获取第一个节点，若为NULL，则设置为NULL）
 * 2.
 * （若原链表有第一个节点）原链表的第一个节点的前一个节点设置为elm
 * 3. 将链表的第一个节点设置为elm
 * 4.
 * 将elm节点的上一个节点设置为链表头（即，le_prev指向链表头中lh_first变量的内存空间，这可通过LIST_FIRST获取，该宏将展开为变量）
 */
#define LIST_INSERT_HEAD(head, elm, field)                                     \
    do {                                                                       \
        if ((LIST_NEXT((elm), field) = LIST_FIRST((head))) != NULL)            \
            LIST_FIRST((head))->field.le_prev = &LIST_NEXT((elm), field);      \
        LIST_FIRST((head)) = (elm);                                            \
        (elm)->field.le_prev = &LIST_FIRST((head));                            \
    } while (0)

/*
 * 展开为链表节点elm中指向下一个节点的链接的变量
 *
 * 即：(elm)->field.le_next
 */
#define LIST_NEXT(elm, field) ((elm)->field.le_next)

/*
 * 将元素 "elm" 从链表中移除。
 * "field" 参数为上述链接元素的结构体字段名。
 *
 * Precondition:
 * - 'elm' 必须是指向有效链表元素的非空指针。
 * - 'elm' 必须已存在于链表中。
 * - 'elm'
 * 可以是链表中的第一个节点（此时，其le_prev指向链表头中lh_first变量的内存空间）
 * - 'elm'可以是链表中的最后一个节点
 *
 * 具体地，该宏执行如下2个操作：
 * 1.（若该节点存在下一个节点）将下一个节点的前一个节点设置为elm的前一个节点
 * 2.
 * 将前一个节点（若为第一个节点，则对应链表头）的下一个节点设置为elm的下一个节点
 */
#define LIST_REMOVE(elm, field)                                                \
    do {                                                                       \
        if (LIST_NEXT((elm), field) != NULL)                                   \
            LIST_NEXT((elm), field)->field.le_prev = (elm)->field.le_prev;     \
        *(elm)->field.le_prev = LIST_NEXT((elm), field);                       \
    } while (0)

/*
 * Tail queue definitions.
 */
#define _TAILQ_HEAD(name, type, qual)                                          \
    struct name {                                                              \
        qual type *tqh_first;      /* first element */                         \
        qual type *qual *tqh_last; /* addr of last next element */             \
    }
/*
 * 链表由 TAILQ_HEAD 宏定义的结构体作为头部。
 * 该结构体包含指向队列中首个元素的指针以及（指向末尾元素的field.tqe_next）的指针。
 * 元素采用双向链接方式，使得无需遍历链表即可删除任意元素。
 * 新元素可被添加到某个已有元素之前、之后，或直接插入队列头部、尾部
 * TAILQ_HEAD 结构体的声明方式如下：
 *
 *       TAILQ_HEAD(HEADNAME, TYPE) head;
 *
 * 其中 HEADNAME 是要定义的结构体名称，TYPE 是要链接到链表中的元素类型。
 *
 * 例如：
 *
 * TAILQ_HEAD(Env_sched_list, Env)
 *
 * 定义了一个名为Env_sched_list的结构体作为队列头，队列的节点类型为Env
 * 注意Env需要单独定义，且包含一个通过TAILQ_ENTRY宏定义的字段以记录链接信息。
 *
 * 当队列非空时，`head->tqh_last` 指向最后一个元素的 `tqe_next` 字段地址。
 *
 * 当队列为空时，`head->tqh_last` 指向 `head->tqh_first` 的地址（即
 * `&head->tqh_first`）。
 */
#define TAILQ_HEAD(name, type) _TAILQ_HEAD(name, struct type, )

/*
 * 将TAILQ_HEAD_INITIALIZER(head)赋值给队列头，可重置其为合法的空队列。
 *
 * 注意当队列为空时，`head.tqh_last`指向存储`head.tqh_first`的内存空间
 *
 */
#define TAILQ_HEAD_INITIALIZER(head) {NULL, &(head).tqh_first}

#define _TAILQ_ENTRY(type, qual)                                               \
    struct {                                                                   \
        qual type *tqe_next;       /* next element */                          \
        qual type *qual *tqe_prev; /* address of previous next element */      \
    }

/*
 * Use this inside a structure "TAILQ_ENTRY(type) field" to use
 * x as the queue piece.
 *
 * tqe_prev指向上一个节点中存储tqe_next的内存区域；
 * 通过使用tqe_prev，可以在不知道队列节点结构体的具体构造的情况下修改节点指向
 * 以完成链表操作
 */
#define TAILQ_ENTRY(type) _TAILQ_ENTRY(struct type, )

/*
 * Tail queue access methods.
 */

/*
 * 返回名为`head`的队列是否为空。
 *
 * Precondition:
 * - `head` 必须是指向合法队列的指针（由TAILQ_INIT初始化）。
 */
#define TAILQ_EMPTY(head) ((head)->tqh_first == NULL)

/*
 * 返回名为`head`的队列中的第一个元素。
 *
 * Precondition:
 * - `head` 必须是指向合法队列的指针（由TAILQ_INIT初始化）。
 *
 * Return：
 * - 队列的第一个节点，若链表为空，返回NULL
 */
#define TAILQ_FIRST(head) ((head)->tqh_first)
/*
 * 展开为队列节点elm中指向下一个节点的链接的变量
 *
 * 即：(elm)->field.tqe_next
 */
#define TAILQ_NEXT(elm, field) ((elm)->field.tqe_next)

/*
 * 获取队列"head"的最后一个元素
 * "headname"是队列头结构体类型名（用于类型转换）
 * 注：通过逆向遍历链表头的前驱指针找到尾节点
 *
 * 详细原理如下：
 *
 * 假设在64位Linux系统上：
 *
 * struct Node {
 *   long data;
 *
 *   TAILQ_ENTRY(Node) link;
 * };
 *
 * TAILQ_HEAD(Head, Node);
 *
 * 则内存结构如下：
 *
 * +------------------+      Node 实例
 * |      data         |    0x1000
 * +------------------+
 * |   tqe_next (8)    |    0x1008
 * +------------------+
 * |   tqe_prev (8)    |    0x1010
 * +------------------+
 *
 * +------------------+      Head 实例
 * |   tqh_first (8)  |    0x2000
 * +------------------+
 * |   tqh_last (8)   |    0x2008
 * +------------------+
 *
 * 注意在Node中，先放置`next`指针，后放置`prev`指针；在Head中，先放置`first`指针，后放置`last`指针，两者相反。、
 *
 * 当队列中有元素时，`(head)->tqh_last`指向队尾元素`tqe_next`所在的内存区域
 * 在上例中，即指向`0x1008`（假设图中的Node实例是队尾元素）
 * `x = (struct headname *)((head)->tqh_last)`指针的作用是：
 * 将`0x1008`开始处的内存按`Head`解析
 *
 * 此时，
 * `x -> tqh_first = node -> tqe_next`
 * `x -> tqh_last = node -> tqe_prev`
 *
 * `TAILQ_LAST`宏的定义相当于：
 *
 * `*(x -> tqh_last) = *(node -> tqe_prev)`
 *
 * 注意`node -> tqe_prev`指向`node`的前一个节点的`next`字段，类型为`Node **`
 * 将其解引用，即得到`node`的前一个节点的`next`字段的值，类型为`Node *`，
 * 即为`Node`节点的地址
 *
 * 当队列中没有元素时，`(head)->tqh_last`指向`(head) -> tqh_first`
 * 则`x = (struct headname *)((head)->tqh_last)`指向队列头本身
 * `x -> tqh_last`仍指向队列头本是，类型是`Node **`
 * 则`*(x -> tqh_last)`相当于从队列头起始处读取8字节，作为返回的`Node *`的值
 * 而“队列头起始处读取8字节”，读取的刚好是`head -> tqh_first = NULL`，返回`NULL`
 */

#define TAILQ_LAST(head, headname)                                             \
    (*(((struct headname *)((head)->tqh_last))->tqh_last))

/*
 * 获取元素"elm"的上一个元素
 * "headname"是队列头结构体类型名（用于类型转换）
 * "field"是元素中TAILQ_ENTRY类型的字段名
 */
#define TAILQ_PREV(elm, headname, field)                                       \
    (*(((struct headname *)((elm)->field.tqe_prev))->tqh_last))

#endif

/*
 * Tail queue functions.
 */

/*
 * 设置/初始化名为"head"的队列头
 */
#define TAILQ_INIT(head)                                                       \
    do {                                                                       \
        (head)->tqh_first = NULL;                                              \
        (head)->tqh_last = &(head)->tqh_first;                                 \
    } while (/*CONSTCOND*/ 0)

/*
 * 将元素"elm"插入到队列"head"的第一个元素。
 * "field"名称即上述链接字段。
 *
 * head、elm均为**指针**
 * filed是类型为`TAILQ_ENTRY(T)`的字段名称
 *
 * Precondition：
 * - `head`必须是指向合法链表的指针（由TAILQ_INIT初始化）。
 * - `elm`必须不为NULL且必须是未链接的节点
 * 注意：
 * - 插入后，elm节点的le_prev将指向链表头中lh_first变量的内存空间
 *
 * 具体地，该宏执行如下4个操作：
 * 1.
 * 将elm节点的下个节点设置为原队列的第一个节点（使用TAILQ_FIRST获取第一个节点，若为NULL，则设置为NULL）
 * 2.
 * （若原队列有第一个节点）原队列的第一个节点的前一个节点设置为elm
 * （若原队列无第一个节点）队列头中中的`tqh_last`指向`elm`的`tqe_next`字段
 * 3. 将队列的第一个节点设置为elm
 * 4.
 * 将elm节点的上一个节点设置为链表头（即，tqe_prev指向队列头中tqh_first变量的内存空间，这可通过TAILQ_FIRST获取，该宏将展开为变量）
 */
#define TAILQ_INSERT_HEAD(head, elm, field)                                    \
    do {                                                                       \
        if (((elm)->field.tqe_next = TAILQ_FIRST(head)) != NULL)               \
            TAILQ_FIRST(head)->field.tqe_prev = &(elm)->field.tqe_next;        \
        else                                                                   \
            (head)->tqh_last = &(elm)->field.tqe_next;                         \
        TAILQ_FIRST(head) = (elm);                                             \
        (elm)->field.tqe_prev = &TAILQ_FIRST(head);                            \
    } while (/*CONSTCOND*/ 0)

/*
 * 将元素"elm"插入到队列"head"的末尾
 * "field"是元素中TAILQ_ENTRY类型的字段名
 *
 * Precondition：
 * - `head`必须是指向合法链表的指针（由TAILQ_INIT初始化）
 * - `elm`必须不为NULL且必须是未链接的节点
 *
 * 具体步骤：
 * 1. 将elm的tqe_next设为NULL（作为新的尾节点）
 * 2. 将elm的tqe_prev指向当前尾节点的tqe_next地址（即head->tqh_last）
 * 3. 将原尾节点的`tqh_next`（可通过`head -> tqh_last`访问）指向"elm"
 * 4. 更新队列头的tqh_last指针指向elm的tqe_next字段
 */
#define TAILQ_INSERT_TAIL(head, elm, field)                                    \
    do {                                                                       \
        (elm)->field.tqe_next = NULL;                                          \
        (elm)->field.tqe_prev = (head)->tqh_last;                              \
        *(head)->tqh_last = (elm);                                             \
        (head)->tqh_last = &(elm)->field.tqe_next;                             \
    } while (/*CONSTCOND*/ 0)

/*
 * 在元素"listelm"之后插入新元素"elm"
 * "field"是元素中TAILQ_ENTRY类型的字段名
 *
 * Precondition：
 * - `listelm`必须已存在于队列"head"中
 * - `elm`必须不为NULL且必须是未链接的节点
 *
 * 具体步骤：
 * 1. 将elm的tqe_next设为listelm的下一个节点
 * 2. 若listelm不是尾节点，设置后一个节点的tqe_prev指向elm
 *    否则更新队列头的tqh_last指向elm的tqe_next
 * 3. 将listelm的tqe_next设为elm
 * 4. 设置elm的tqe_prev指向listelm的tqe_next字段地址
 */
#define TAILQ_INSERT_AFTER(head, listelm, elm, field)                          \
    do {                                                                       \
        if (((elm)->field.tqe_next = (listelm)->field.tqe_next) != NULL)       \
            (elm)->field.tqe_next->field.tqe_prev = &(elm)->field.tqe_next;    \
        else                                                                   \
            (head)->tqh_last = &(elm)->field.tqe_next;                         \
        (listelm)->field.tqe_next = (elm);                                     \
        (elm)->field.tqe_prev = &(listelm)->field.tqe_next;                    \
    } while (/*CONSTCOND*/ 0)

/*
 * 在元素"listelm"之前插入新元素"elm"
 * "field"是元素中TAILQ_ENTRY类型的字段名
 *
 * Precondition：
 * - `listelm`必须已存在于队列中
 * - `elm`必须不为NULL且必须是未链接的节点
 *
 * 具体步骤：
 * 1. 将elm的tqe_prev设为listelm的tqe_prev
 * 2. 将elm的tqe_next设为listelm
 * 3. 通过解引用listelm的tqe_prev设置前驱节点的tqe_next
 * 4. 将listelm的tqe_prev指向elm的tqe_next字段地址
 */
#define TAILQ_INSERT_BEFORE(listelm, elm, field)                               \
    do {                                                                       \
        (elm)->field.tqe_prev = (listelm)->field.tqe_prev;                     \
        (elm)->field.tqe_next = (listelm);                                     \
        *(listelm)->field.tqe_prev = (elm);                                    \
        (listelm)->field.tqe_prev = &(elm)->field.tqe_next;                    \
    } while (/*CONSTCOND*/ 0)

/*
 * 从队列"head"中移除元素"elm"
 * "field"是元素中TAILQ_ENTRY类型的字段名
 *
 * 具体步骤：
 * 1. 若elm不是尾节点，更新后驱节点的tqe_prev
 *    否则更新队列头的tqh_last指向elm的tqe_prev
 * 2. 通过解引用elm的tqe_prev更新前驱节点的tqe_next
 */
#define TAILQ_REMOVE(head, elm, field)                                         \
    do {                                                                       \
        if (((elm)->field.tqe_next) != NULL)                                   \
            (elm)->field.tqe_next->field.tqe_prev = (elm)->field.tqe_prev;     \
        else                                                                   \
            (head)->tqh_last = (elm)->field.tqe_prev;                          \
        *(elm)->field.tqe_prev = (elm)->field.tqe_next;                        \
    } while (/*CONSTCOND*/ 0)

/*
 * 正向遍历名为"head"的队列中的元素。
 * 在循环过程中，将队列元素赋值给变量"var"
 * 其中，"field"对应队列节点结构体中TAILQ_ENTRY类型的字段的名称。
 *
 * 使用方法：
 * TAILQ_FOREACH(var, head, field) {
 *    // operation...
 * }
 */
#define TAILQ_FOREACH(var, head, field)                                        \
    for ((var) = ((head)->tqh_first); (var); (var) = ((var)->field.tqe_next))

/*
 * 逆向遍历名为"head"的队列中的元素。
 * 在循环过程中，将队列元素赋值给变量"var"
 * 其中，"field"对应队列节点结构体中TAILQ_ENTRY类型的字段的名称。
 *
 * 使用方法：
 * TAILQ_FOREACH_REVERSE(var, head, field) {
 *    // operation...
 * }
 */
#define TAILQ_FOREACH_REVERSE(var, head, headname, field)                      \
    for ((var) = (*(((struct headname *)((head)->tqh_last))->tqh_last));       \
         (var);                                                                \
         (var) = (*(((struct headname *)((var)->field.tqe_prev))->tqh_last)))

/*
 * 将队列"head2"的所有元素连接到"head1"的尾部
 * "field"是元素中TAILQ_ENTRY类型的字段名
 *
 * 操作后：
 * - head1将包含原head1和head2的所有元素
 * - head2会被重新初始化为空队列
 */
#define TAILQ_CONCAT(head1, head2, field)                                      \
    do {                                                                       \
        if (!TAILQ_EMPTY(head2)) {                                             \
            *(head1)->tqh_last = (head2)->tqh_first;                           \
            (head2)->tqh_first->field.tqe_prev = (head1)->tqh_last;            \
            (head1)->tqh_last = (head2)->tqh_last;                             \
            TAILQ_INIT((head2));                                               \
        }                                                                      \
    } while (/*CONSTCOND*/ 0)
