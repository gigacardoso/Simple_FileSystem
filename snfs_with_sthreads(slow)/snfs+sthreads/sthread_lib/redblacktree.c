#include "redblacktree.h"

void warning(){
	exe_thr_tree=NULL;
}

rbtree rbCreate(){
	rbtree tree;
	rbnode temp;
	
	tree=(rbtree) malloc(sizeof(struct redblacktree));
	tree->prioritary=NULL;
	temp=tree->nil= (rbnode) malloc(sizeof(struct redblacknode));
  	temp->parent=temp->left=temp->right=temp;
  	temp->red=0;
  	temp->thread = NULL;
  	temp=tree->first= (rbnode) malloc(sizeof(struct redblacknode));
  	temp->parent = temp->left=temp->right=tree->nil;
  	temp->thread = NULL;
  	temp->red=0;
	
	return tree;
}

//compares two Tree nodes by their vruntimes
int compare(rbnode a, rbnode b){
	return a->thread->vruntime >= b->thread->vruntime;
}


rbnode rbSearch(rbtree tree,rbnode node, int n, int v) {
	if (node == tree->nil)
		return node;
	if (n == node->thread->tid)
		return node;
	if (v < node->thread->vruntime)
		return rbSearch(tree,node->left, n, v);
	else
		return rbSearch(tree,node->right, n, v);
}


void leftRotate(rbtree tree, rbnode  x) {
  rbnode  y;
 
  y=x->right;
  x->right=y->left;

  if (y->left != tree->nil) y->left->parent=x; /* used to use sentinel here */
  /* and do an unconditional assignment instead of testing for nil */
  
  y->parent=x->parent;   

  /* instead of checking if x->parent is the root as in the book, we */
  /* count on the root sentinel to implicitly take care of this case */
  if( x == x->parent->left) {
    x->parent->left=y;
  } else {
    x->parent->right=y;
  }
  y->left=x;
  x->parent=y;
}




void rightRotate(rbtree tree, rbnode  y) {
  rbnode  x;

	x=y->left;
  y->left=x->right;

  if (tree->nil != x->right)  x->right->parent=y; /*used to use sentinel here */
  /* and do an unconditional assignment instead of testing for nil */

  /* instead of checking if x->parent is the root as in the book, we */
  /* count on the root sentinel to implicitly take care of this case */
  x->parent=y->parent;
  if( y == y->parent->left) {
    y->parent->left=x;
  } else {
    y->parent->right=x;
  }
  x->right=y;
  y->parent=x;
}

//maintaints the RBTree balanced and its carcteristics after insertion of a node
void treeInsertHelp(rbtree tree, rbnode z) {
  /*  This function should only be called by InsertRBTree (see above) */
  rbnode x;
  rbnode y;

  
  z->left=z->right=tree->nil;
  y=tree->first; 
  x=tree->first->left;
  while( x != tree->nil) {
    y=x;
    if (compare(x,z)) { /* x.key >= z.key */
      x=x->left;
    } else { /* x,key <= z.key */
      x=x->right;
    }
  }
  z->parent=y;
  if ( (y == tree->first) ||
       (compare(y,z))) { /* y.key >= z.key */
    y->left=z;
    if((y == tree->first) || tree->prioritary->left != tree->nil)
    	tree->prioritary=z;      
  } else {
    y->right=z;
  }
}


rbnode rbTreeInsert(rbtree tree,sthread_t key) {
  rbnode  y;
  rbnode  x;
  rbnode  newNode;

  x= (rbnode ) malloc(sizeof(struct redblacknode));
  x->thread=key;
 
  treeInsertHelp(tree,x);
  newNode=x;
  x->red=1;
  while(x->parent->red) { /* use sentinel instead of checking for root */
    if (x->parent == x->parent->parent->left) {
      y=x->parent->parent->right;
      if (y->red) {
	x->parent->red=0;
	y->red=0;
	x->parent->parent->red=1;
	x=x->parent->parent;
      } else {
	if (x == x->parent->right) {
	  x=x->parent;
	  leftRotate(tree,x);
	}
	x->parent->red=0;
	x->parent->parent->red=1;
	rightRotate(tree,x->parent->parent);
      } 
    } else { /* case for x->parent == x->parent->parent->right */
      y=x->parent->parent->left;
      if (y->red) {
	x->parent->red=0;
	y->red=0;
	x->parent->parent->red=1;
	x=x->parent->parent;
      } else {
	if (x == x->parent->left) {
	  x=x->parent;
	  rightRotate(tree,x);
	}
	x->parent->red=0;
	x->parent->parent->red=1;
	leftRotate(tree,x->parent->parent);
      } 
    }
  }
  tree->first->left->red=0;
  return newNode;
}

//maintaints the RBTree balanced and its carcteristics after deletion of a node
void dColorBalance(rbtree tree,rbnode newnode){
	
	rbnode temp;//define a temporary node named temp
	while (newnode != tree->first && newnode->red ==0) {//repeat while newnode is not root and its color is black
		if (newnode == newnode->parent->left) {//check whether newnode is its parent's left child
			temp = newnode->parent->right;//set right child of newnode's parent to temp
			if (temp->red ==1) {  //check whether color of temp is red
				temp->red = 0;  //set color of newnode to black
				newnode->parent->red = 1;//set color of newnode's parent to red
				leftRotate(tree,newnode->parent); // call left rotation function
				temp = newnode->parent->right; // set right child of newnode's parent to temp
			}
			if (temp->left->red ==0 && temp->right->red == 0) {
				temp->red = 1; //set color of temp to red
				newnode = newnode->parent;// set parent of newnode to newnode
			}
			else {
				if (temp->right->red ==0) {//check whether color of temp's right child is black
					temp->left->red =0; //set color of temp's left child to black
					temp->red =1;//set color of temp to red
					rightRotate(tree,temp); // call right rotation function
					temp = newnode->parent->right;//set right child of newnode's parent to temp
				}
				temp->red = newnode->parent->red;//set color of newnode's parent to color of temp
				newnode->parent->red =0;//set color of newnode's parent to black
				temp->right->red = 0;//set color of newnode's right child to black
				leftRotate(tree,newnode->parent); //call left rotation function
				newnode = tree->first;  //set root to newnode
			}
		}
		else {
			temp = newnode->parent->left;//set left child of newnode's parent to temp
			if (temp->red == 1) { //check whether color of temp is red
				temp->red = 0;  //set color of temp to black
				newnode->parent->red =1;//set color of newnode's parent to red
				rightRotate(tree,newnode->parent); // call right rotation function
				temp = newnode->parent->left;//set left child of newnode's parent to temp
			}
			if(temp->left==tree->nil || temp->right==tree->nil)
				if (temp->right->red == 0 || temp->left->red == 0) {
					temp->red = 1; //set color of temp to red
					newnode = newnode->parent;//set parent of newnode to newnode
				}
				else {
					temp->red = 1; //set color of temp to red
					newnode = newnode->parent;//set parent of newnode to newnode
				}
			else {
				if (temp->left->red ==0) {//check whether color of temp's left child is black
					temp->right->red = 0; //set color of newnode's right child to black
					temp->red =1; //set color of newnode to red
					leftRotate(tree,temp); // call left rotation function
					temp = newnode->parent->left;//set left child of newnode's parent to temp
				}
				temp->red = newnode->parent->red; //set color of newnode'sa parent to red of temp
				newnode->parent->red =0;//set color of newnode's parent
				temp->left->red =0; //set color of temp's left child to black
				rightRotate(tree,newnode->parent); // call right rotation function
				newnode =tree->first; //set root to newnode
			}
		}
	}
	newnode->red =0;// set color of newnode to black
}

//deletes a node
void rbDelete(rbtree tree,rbnode node){

	rbnode newnode; //define a temporary node named newnode
	rbnode temp;//define a temporary node named newnode
	if(node->left==tree->nil || node->right==tree->nil)//check whether node has any child
		temp=node; //set the node to temp
	else{
		temp = node->right; //set right child of node to temp
		while (temp->left != tree->nil)//repeat while left child of temp is not NULL
			temp = temp->left;
	}
	if (temp->left != tree->nil)//check whether left child of temp is not NULL
		newnode= temp->left; //set left child of temp to newnode
	else
		newnode = temp->right;//set right child of temp to newnode
	newnode->parent = temp->parent;// set parent of temp to parent of newnode
	if (temp->parent) // check whether temp has parent
		if (temp == temp->parent->left)//check whether temp is its parent's left child
			temp->parent->left = newnode; //set newnode to left child of temp's parent
		else
			temp->parent->right = newnode;//set newnode to right child of temp's parent
	else
		tree->first= newnode;  //set newnode to root
	if (temp != node)  //check whether tree->location is not same as temp
		node->thread = temp->thread;//set value of temp to value of tree->location
	if (temp->red == 0) //check whether color of newnode id black
		dColorBalance(tree, newnode);// call deletion color balance function

	free(temp);    //delete node from memory
}

//traverses the Tree inorder and increments every node's waittime
void inorder(rbtree tree, rbnode ptr,int delay)
{
	if(ptr != tree->nil){
		inorder(tree, ptr->left,delay);//call inorder function again
		ptr->thread->waittime += delay;// print values and colors
		inorder(tree, ptr->right,delay);// call inorder function again
	}
}

void travers(rbtree tree,int delay)
{
   inorder(tree, tree->first->left,delay); // call inorder travers function
}

//Auciliar function to Destroy tree
void treeDestHelper(rbtree tree, rbnode x) {
  rbnode nil=tree->nil;
  if (x != nil) {
    treeDestHelper(tree,x->left);
    treeDestHelper(tree,x->right);
    free(x);
  }
}

void rbTreeDestroy(rbtree tree) {
  treeDestHelper(tree,tree->first->left);
  treeDestHelper(tree,tree->first->right);
  free(tree->first);
  free(tree->nil);
  free(tree);
}

//Checks if the tree is empty return 1 if so, ans 0 if it is not
int emptyTree(rbtree tree){
	return tree->first->left == tree->nil;
}

// Removes the priority node of the tree( the one on the left)
sthread_t treeRemove(rbtree tree){
	sthread_t temp;
	rbnode temp1;
	temp = tree->prioritary->thread;
	temp1= tree->prioritary;
	if(tree->prioritary->right != tree->nil)
		tree->prioritary= tree->prioritary->right;
	else{
		if(tree->prioritary->parent != tree->first && tree->prioritary->parent != tree->nil)
			tree->prioritary=tree->prioritary->parent;
		else
			tree->prioritary=tree->nil;
	}
	if(temp1 != tree->nil)
		rbDelete(tree, temp1);
	return temp;
}


//Prints Tree Info inorder
void inorderDump(rbtree tree, rbnode ptr)
{
	if(ptr != tree->nil){
		inorderDump(tree, ptr->left);//call inorder function again
		printf("id: %d priority: %d vruntime: %ld\nruntime: %ld sleeptime: %ld waittime: %ld\n\n", ptr->thread->tid, ptr->thread->priority, ptr->thread->vruntime, ptr->thread->runtime, ptr->thread->sleeptime, ptr->thread->waittime );
		inorderDump(tree, ptr->right);// call inorder function again
	}
}

void dumpTree(rbtree tree){
	printf(">>>> RB-Tree <<<<\n\n");
	
	inorderDump(tree, tree->first->left);
	
}

//Decrements by dec the Vruntime values on all the tree nodes
// USed To Treat Overflow
void inorderDec(rbtree tree,rbnode ptr, long dec)
{
	if(ptr != tree->nil){
		inorderDec(tree, ptr->left, dec);//call inorder function again
		ptr->thread->vruntime -= dec;
		inorderDec(tree, ptr->right, dec);// call inorder function again
	}
}

//dec Tree Nodes
void decTree(rbtree tree,long dec){
	inorderDec(tree, tree->first->left, dec);
}
