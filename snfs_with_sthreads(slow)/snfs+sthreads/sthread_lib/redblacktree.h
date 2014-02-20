#include <stdio.h>
#include <stdlib.h>

#include <sthread.h>
#include <sthread_user.h>
#include <sthread_user.h>

typedef struct redblacknode      // class to define a node
{
	sthread_t thread;        //the thread part of node
	int red;       // color part of node( 1 if red 0 if black)
	struct redblacknode *left;    // pointer to left child of node
	struct redblacknode *right;   // pointer to right child of node
	struct redblacknode *parent;  // pointer to parent of node
}* rbnode;

struct redblacktree{
	rbnode first;    // pointer to root of the tree
	rbnode prioritary;  // pointer to location of a node to operate
	rbnode nil;
};

typedef struct redblacktree * rbtree;

rbtree rbCreate();

rbnode rbTreeInsert(rbtree tree, sthread_t key);

void rbTreeDestroy(rbtree tree);

int emptyTree(rbtree tree);

sthread_t treeRemove(rbtree);

rbnode rbSearch(rbtree tree,rbnode node, int n, int v);

void dumpTree(rbtree tree);

void travers(rbtree tree,int delay);

void decTree(rbtree tree,long dec);

static rbtree	exe_thr_tree;		/* árvore de threads executaveis */
