#include "tree.h"
#include "logger.h"
#include "type.h"
#include "zwm.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <xcb/xcb.h>
#include <xcb/xcb_icccm.h>

#define MAX(a, b)                                                              \
	({                                                                         \
		__typeof__(a) _a = (a);                                                \
		__typeof__(b) _b = (b);                                                \
		_a > _b ? _a : _b;                                                     \
	})

node_t *create_node(client_t *c) {
	if (c == 0x00) {
		return NULL;
	}

	node_t *node = (node_t *)malloc(sizeof(node_t));
	if (node == 0x00) {
		return NULL;
	}

	node->client	   = c;
	node->id		   = 1;
	node->parent	   = NULL;
	node->first_child  = NULL;
	node->second_child = NULL;
	node->is_master	   = false;
	node->is_focused   = false;

	return node;
}

node_t *init_root() {
	node_t *node = (node_t *)malloc(sizeof(node_t));
	if (node == 0x00) {
		return NULL;
	}

	node->client	   = NULL;
	node->id		   = 1;
	node->parent	   = NULL;
	node->first_child  = NULL;
	node->second_child = NULL;
	node->node_type	   = ROOT_NODE;
	node->is_master	   = false;
	node->is_focused   = false;

	return node;
}

int render_tree(node_t *node) {
	if (node == NULL) {
		return 0;
	}

	if (node->node_type != INTERNAL_NODE && node->client != NULL) {
		if (node->client->state == FULLSCREEN ||
			node->client->state == FLOATING) {
			raise_window(wm->connection, node->client->window);
			goto skip_;
		}
		if (tile(node) != 0) {
			log_message(ERROR, "error tiling window %d", node->client->window);
			return -1;
		}
		lower_window(wm->connection, node->client->window);
	}

skip_:
	// if (node->first_child != NULL)
	render_tree(node->first_child);
	// if (node->second_child != NULL)
	render_tree(node->second_child);

	return 0;
}

int get_tree_level(node_t *node) {
	if (node == NULL) {
		return 0;
	}

	int level_first_child  = get_tree_level(node->first_child);
	int level_second_child = get_tree_level(node->second_child);

	return 1 + MAX(level_first_child, level_second_child);
}

void insert_floating_node(node_t *node, desktop_t *d) {

	assert(node->client->state == FLOATING);
	node_t *n = find_left_leaf(d->tree);
	if (n == NULL) {
		return;
	}

	if (n->first_child == NULL) {
		n->first_child = node;
	} else {
		n->second_child = node;
	}
	node->node_type = EXTERNAL_NODE;
}

void insert_node(node_t *node, node_t *new_node) {
	if (node == NULL) {
		log_message(ERROR, "node is null");
		return;
	}

	if (node->client == NULL) {
		log_message(ERROR, "client is null in node");
		return;
	}

	if (node->node_type != ROOT_NODE) {
		node->node_type = INTERNAL_NODE;
	}

	if (!has_first_child(node)) {
		node->first_child = create_node(node->client);
		if (node->first_child == NULL) {
			return;
		}
		node->first_child->parent = node;
		rectangle_t r			  = {0};
		if (node->rectangle.width >= node->rectangle.height) {
			r.x		 = node->rectangle.x;
			r.y		 = node->rectangle.y;
			r.width	 = (node->rectangle.width - W_GAP) / 2;
			r.height = node->rectangle.height;
		} else {
			r.x		 = node->rectangle.x;
			r.y		 = node->rectangle.y;
			r.width	 = node->rectangle.width;
			r.height = (node->rectangle.height - W_GAP) / 2;
		}
		node->first_child->rectangle = r;
		node->first_child->node_type = EXTERNAL_NODE;
		node->client				 = NULL;
	}

	if (!has_second_child(node)) {
		node->second_child = new_node;
		if (node->second_child == NULL) {
			return;
		}
		node->second_child->parent = node;
		rectangle_t r			   = {0};
		if (node->rectangle.width >= node->rectangle.height) {
			r.x		= (int16_t)(node->rectangle.x +
							node->first_child->rectangle.width + W_GAP);
			r.y		= node->rectangle.y;
			r.width = node->rectangle.width -
					  node->first_child->rectangle.width - W_GAP;
			r.height = node->rectangle.height;
		} else {
			r.x		 = node->rectangle.x;
			r.y		 = (int16_t)(node->rectangle.y +
							 node->first_child->rectangle.height + W_GAP);
			r.width	 = node->rectangle.width;
			r.height = node->rectangle.height -
					   node->first_child->rectangle.height - W_GAP;
		}
		node->second_child->rectangle = r;
		node->second_child->node_type = EXTERNAL_NODE;
	}
}

node_t *find_node_by_window_id(node_t *root, xcb_window_t win) {
	if (root == NULL) {
		return NULL;
	}

	if (root->client != NULL && root->client->window == win) {
		return root;
	}

	node_t *left_result = find_node_by_window_id(root->first_child, win);
	if (left_result != NULL) {
		return left_result;
	}

	node_t *right_result = find_node_by_window_id(root->second_child, win);
	if (right_result != NULL) {
		return right_result;
	}

	return NULL;
}

void free_tree(node_t *root) {
	if (root == NULL) {
		return;
	}

	if (root->client != NULL) {
		free(root->client);
		root->client = NULL;
	}

	node_t *f = root->first_child;
	free_tree(f);
	root->first_child = NULL;
	node_t *s		  = root->second_child;
	free_tree(s);
	root->second_child = NULL;
	free(root);
}

void resize_subtree(node_t *parent) {
	if (parent == NULL) {
		return;
	}

	rectangle_t r, r2 = {0};

	if (parent->rectangle.width >= parent->rectangle.height) {
		r.x		  = parent->rectangle.x;
		r.y		  = parent->rectangle.y;
		r.width	  = (parent->rectangle.width - W_GAP) / 2;
		r.height  = parent->rectangle.height;

		r2.x	  = (int16_t)(parent->rectangle.x + r.width + W_GAP);
		r2.y	  = parent->rectangle.y;
		r2.width  = parent->rectangle.width - r.width - W_GAP;
		r2.height = parent->rectangle.height;
	} else {
		r.x		  = parent->rectangle.x;
		r.y		  = parent->rectangle.y;
		r.width	  = parent->rectangle.width;
		r.height  = (parent->rectangle.height - W_GAP) / 2;

		r2.x	  = parent->rectangle.x;
		r2.y	  = (int16_t)(parent->rectangle.y + r.height + W_GAP);
		r2.width  = parent->rectangle.width;
		r2.height = parent->rectangle.height - r.height - W_GAP;
	}

	parent->first_child->rectangle	= r;
	parent->second_child->rectangle = r2;

	if (parent->first_child->node_type == INTERNAL_NODE) {
		resize_subtree(parent->first_child);
	}

	if (parent->second_child->node_type == INTERNAL_NODE) {
		resize_subtree(parent->second_child);
	}
}

node_t *find_master_node(node_t *root) {
	if (root == NULL) {
		return NULL;
	}

	if (root->is_master) {
		return root;
	}

	node_t *left_result = find_master_node(root->first_child);
	if (left_result != NULL) {
		return left_result;
	}

	node_t *right_result = find_master_node(root->second_child);
	if (right_result != NULL) {
		return right_result;
	}

	return NULL;
}

bool has_sibling(const node_t *node) {
	if (node == NULL || node->parent == NULL) {
		return false;
	}

	const node_t *parent = node->parent;

	return (parent->first_child != NULL && parent->second_child != NULL);
}

bool has_internal_sibling(const node_t *node) {
	if (node == NULL || node->parent == NULL) {
		return false;
	}

	const node_t *parent = node->parent;

	return (parent->first_child != NULL && parent->second_child != NULL) &&
		   ((parent->first_child->node_type == INTERNAL_NODE) ||
			(parent->second_child->node_type == INTERNAL_NODE));
}

bool is_sibling_external(const node_t *node) {
	if (node == NULL || node->parent == NULL) {
		return false;
	}

	node_t		 *parent  = node->parent;
	const node_t *sibling = (parent->first_child == node) ? parent->second_child
														  : parent->first_child;

	return (sibling != NULL && sibling->node_type == EXTERNAL_NODE);
}

node_t *get_external_sibling(const node_t *node) {
	if (node == NULL || node->parent == NULL) {
		return NULL;
	}

	const node_t *parent  = node->parent;
	node_t		 *sibling = (parent->first_child == node) ? parent->second_child
														  : parent->first_child;

	return (sibling != NULL && sibling->node_type == EXTERNAL_NODE) ? sibling
																	: NULL;
}

bool is_sibling_internal(const node_t *node) {
	if (node == NULL || node->parent == NULL) {
		return false;
	}

	node_t		 *parent  = node->parent;
	const node_t *sibling = (parent->first_child == node) ? parent->second_child
														  : parent->first_child;

	return (sibling != NULL && sibling->node_type == INTERNAL_NODE);
}

node_t *get_internal_sibling(node_t *node) {
	if (node == NULL || node->parent == NULL) {
		return NULL;
	}

	node_t *parent	= node->parent;
	node_t *sibling = (parent->first_child == node) ? parent->second_child
													: parent->first_child;

	return (sibling != NULL && sibling->node_type == INTERNAL_NODE) ? sibling
																	: NULL;
}

node_t *get_sibling(node_t *node, node_type_t *type) {
	if (node == NULL || node->parent == NULL) {
		return NULL;
	}

	node_t *parent	= node->parent;
	node_t *sibling = (parent->first_child == node) ? parent->second_child
													: parent->first_child;

	switch (*type) {
	case INTERNAL_NODE: {
		return (sibling != NULL && sibling->node_type == INTERNAL_NODE)
				   ? sibling
				   : NULL;
	}
	case EXTERNAL_NODE: {
		return (sibling != NULL && sibling->node_type == EXTERNAL_NODE)
				   ? sibling
				   : NULL;
	}
	case ROOT_NODE: break;
	}
	return NULL;
}

bool has_external_children(const node_t *parent) {
	return (parent->first_child != NULL &&
			parent->first_child->node_type == EXTERNAL_NODE) &&
		   (parent->second_child != NULL &&
			parent->second_child->node_type == EXTERNAL_NODE);
}

node_t *find_tree_root(node_t *node) {
	if (node->node_type == ROOT_NODE) {
		return node;
	}
	return find_tree_root(node->parent);
}

bool has_single_external_child(const node_t *parent) {
	if (parent == NULL) return false;

	return ((parent->first_child != NULL && parent->second_child != NULL) &&
			(parent->first_child->node_type == EXTERNAL_NODE &&
			 parent->second_child->node_type != EXTERNAL_NODE)) ||
		   ((parent->first_child != NULL && parent->second_child != NULL) &&
			(parent->second_child->node_type == EXTERNAL_NODE &&
			 parent->first_child->node_type != EXTERNAL_NODE));
}

client_t *find_client_by_window_id(node_t *root, xcb_window_t win) {
	if (root == NULL) {
		return NULL;
	}

	if (root->client != NULL && root->client->window == win) {
		return root->client;
	}

	node_t *left_result = find_node_by_window_id(root->first_child, win);
	if (left_result == NULL) {
		return NULL;
	}

	if (left_result->client != NULL) {
		return left_result->client;
	}

	node_t *right_result = find_node_by_window_id(root->second_child, win);
	if (right_result == NULL) {
		return NULL;
	}

	if (right_result->client != NULL) {
		return right_result->client;
	}

	return NULL;
}

void apply_default_layout(node_t *root) {
	if (root == NULL) {
		return;
	}

	rectangle_t r, r2 = {0};

	if (root->rectangle.width >= root->rectangle.height) {
		r.x		  = root->rectangle.x;
		r.y		  = root->rectangle.y;
		r.width	  = (root->rectangle.width - W_GAP) / 2;
		r.height  = root->rectangle.height;

		r2.x	  = (int16_t)(root->rectangle.x + r.width + W_GAP);
		r2.y	  = root->rectangle.y;
		r2.width  = root->rectangle.width - r.width - W_GAP;
		r2.height = root->rectangle.height;
	} else {
		r.x		  = root->rectangle.x;
		r.y		  = root->rectangle.y;
		r.width	  = root->rectangle.width;
		r.height  = (root->rectangle.height - W_GAP) / 2;

		r2.x	  = root->rectangle.x;
		r2.y	  = (int16_t)(root->rectangle.y + r.height + W_GAP);
		r2.width  = root->rectangle.width;
		r2.height = root->rectangle.height - r.height - W_GAP;
	}

	root->first_child->rectangle  = r;
	root->second_child->rectangle = r2;

	if (root->first_child->node_type == INTERNAL_NODE) {
		apply_default_layout(root->first_child);
	}

	if (root->second_child->node_type == INTERNAL_NODE) {
		apply_default_layout(root->second_child);
	}
}

void default_layout(node_t *root) {
	rectangle_t	   r = {0};
	const uint16_t w = wm->screen->width_in_pixels;
	const uint16_t h = wm->screen->height_in_pixels;

	if (wm->bar != NULL) {
		xcb_get_geometry_reply_t *g =
			get_geometry(wm->bar->window, wm->connection);
		r.x		 = W_GAP - BORDER_WIDTH * 1.5;
		r.y		 = (int16_t)(g->height + W_GAP);
		r.width	 = (uint16_t)(w - 2 * W_GAP);
		r.height = (uint16_t)(h - 2 * W_GAP - g->height);
		free(g);
		g = NULL;
	} else {
		r.x		 = W_GAP;
		r.y		 = W_GAP;
		r.width	 = w - W_GAP;
		r.height = h - W_GAP;
	}
	root->rectangle = r;
	log_message(DEBUG, " HHHHHHHHHHHHconst char *format, ..");
	apply_default_layout(root);
}

void apply_master_layout(node_t *parent) {
	if (parent == NULL) {
		return;
	}

	if (parent->first_child->is_master) {
		parent->second_child->rectangle = parent->rectangle;
	} else if (parent->second_child->is_master) {
		parent->first_child->rectangle = parent->rectangle;
	} else {
		rectangle_t r, r2 = {0};
		r.x		  = parent->rectangle.x;
		r.y		  = parent->rectangle.y;
		r.width	  = parent->rectangle.width;
		r.height  = (parent->rectangle.height - W_GAP) / 2;
		// r.height  = h;
		r2.x	  = parent->rectangle.x;
		r2.y	  = (int16_t)(parent->rectangle.y + r.height + W_GAP);
		r2.width  = parent->rectangle.width;
		r2.height = parent->rectangle.height - r.height - W_GAP;
		// r2.height = h;

		parent->first_child->rectangle	= r;
		parent->second_child->rectangle = r2;
	}

	if (parent->first_child->node_type == INTERNAL_NODE) {
		apply_master_layout(parent->first_child);
	}

	if (parent->second_child->node_type == INTERNAL_NODE) {
		apply_master_layout(parent->second_child);
	}
}

void master_layout(node_t *root) {
	const double ratio		  = 0.70;
	uint64_t	 w			  = wm->screen->width_in_pixels;
	uint64_t	 h			  = wm->screen->height_in_pixels;
	uint16_t	 master_width = w * ratio;
	uint16_t	 r_width	  = (uint16_t)(w * (1 - ratio));
	// uint16_t	 r_height	  = (uint16_t)(h - 2 * W_GAP - 27) / (nc - 1);

	xcb_window_t win = get_window_under_cursor(wm->connection, wm->root_window);
	if (win == XCB_NONE || win == wm->root_window) {
		return;
	}
	node_t *n = find_node_by_window_id(root, win);
	if (n == NULL) {
		return;
	}
	n->is_master   = true;
	// uint16_t r_height = (uint16_t)(h - (2 * W_GAP - 27)) / (nc - 1) - 2 *
	// W_GAP;
	// uint16_t height = (r2.height / (nc - 1)) - (W_GAP / 2) - 2;

	rectangle_t r1 = {
		.x		= W_GAP,
		.y		= (int16_t)(27 + W_GAP),
		.width	= (uint16_t)(master_width - 2 * W_GAP),
		.height = (uint16_t)(h - 2 * W_GAP - 27),
	};
	rectangle_t r2 = {
		.x		= (master_width),
		.y		= (int16_t)(27 + W_GAP),
		.width	= (uint16_t)(r_width - (1 * W_GAP)),
		.height = (uint16_t)(h - 2 * W_GAP - 27),
		// .height = r_height,
	};
	n->rectangle	= r1;
	root->rectangle = r2;
	apply_master_layout(root);
	n->is_master = false;
}
void apply_stack_layout(node_t *root) {
}

void stack_layout(node_t *root) {
	rectangle_t	   r = {0};
	const uint16_t w = wm->screen->width_in_pixels;
	const uint16_t h = wm->screen->height_in_pixels;
	if (wm->bar != NULL) {
		xcb_get_geometry_reply_t *g =
			get_geometry(wm->bar->window, wm->connection);
		r.x		 = W_GAP - BORDER_WIDTH * 1.5;
		r.y		 = (int16_t)(g->height + W_GAP);
		r.width	 = (uint16_t)(w - 2 * W_GAP);
		r.height = (uint16_t)(h - 2 * W_GAP - g->height);
		free(g);
		g = NULL;
	} else {
		r.x		 = W_GAP;
		r.y		 = W_GAP;
		r.width	 = w - W_GAP;
		r.height = h - W_GAP;
	}
	root->rectangle = r;
}

void grid_layout(node_t *root) {
}
void apply_layout(desktop_t *d, layout_t t) {
	d->layout	 = t;
	node_t *root = d->tree;
	switch (t) {
	case DEFAULT: {
		default_layout(root);
		log_message(DEBUG, " IIIIIIIIIIIIIIIIIIIIII char *format, ..");
		break;
	}
	case MASTER: {
		master_layout(root);
		break;
	}
	case STACK: {
		stack_layout(root);
		break;
	}
	case GRID: {
		break;
	}
	}
}

int delete_node_with_external_sibling(node_t *node) {
	/* node to delete = N, internal node = I, external node = E
	 *         I
	 *    	 /   \
	 *     N||E   N||E
	 *
	 * logic:
	 * just delete N and replace E with I and give it full I's rectangle
	 */
	node_t *external_node = NULL;
	assert(is_sibling_external(node));
	external_node = get_external_sibling(node);

	if (external_node == NULL) {
		log_message(ERROR, "external node is null");
		return -1;
	}

	// if I has no parent
	if (node->parent->parent == NULL) {
		node->parent->node_type	   = ROOT_NODE;
		node->parent->client	   = external_node->client;
		node->parent->first_child  = NULL;
		node->parent->second_child = NULL;
	} else {
		// if I has a prent
		/*
		 *         I
		 *    	 /   \
		 *   	E     I
		 *    	 	/   \
		 *   	   N     E
		 */
		node_t *grandparent = node->parent->parent;
		if (grandparent->first_child == node->parent) {
			grandparent->first_child->node_type	   = EXTERNAL_NODE;
			grandparent->first_child->client	   = external_node->client;
			grandparent->first_child->first_child  = NULL;
			grandparent->first_child->second_child = NULL;
		} else {
			grandparent->second_child->node_type	= EXTERNAL_NODE;
			grandparent->second_child->client		= external_node->client;
			grandparent->second_child->first_child	= NULL;
			grandparent->second_child->second_child = NULL;
		}
	}
	free(external_node);
	external_node = NULL;
	free(node->client);
	node->client = NULL;
	free(node);
	node = NULL;
	return 0;
}

int delete_node_with_internal_sibling(node_t *node, desktop_t *d) {
	if (d == NULL) {
		return -1;
	}
	/* node to delete = N
	 * internal node (parent of N) = IN
	 * external node 1 = E1, external node 2 = E2
	 * internal node (parent of Es) = IE
	 *             IN
	 *       	 /   \
	 *          N     IE
	 *                / \
	 *              E1   E2
	 *
	 * logic: IN->IE = NULL, IN->N = NULL then link IN with E1,E2.
	 * lastly unlink IE->IN, IE->E1, IE->E2, N->IN and free N & IE
	 */
	node_t *internal_sibling = NULL;
	// if IN has no parent
	if (node->parent->parent == NULL) {
		if (is_sibling_internal(node)) {
			internal_sibling = get_internal_sibling(node);
		}

		if (internal_sibling == NULL) {
			log_message(ERROR, "internal node is null");
			return -1;
		}

		internal_sibling->rectangle = node->parent->rectangle;
		internal_sibling->parent	= NULL;
		internal_sibling->node_type = ROOT_NODE;
		if (d->tree == node->parent) {
			node->parent = NULL;
			if (d->tree->first_child == node) {
				d->tree->first_child = internal_sibling->first_child;
				internal_sibling->first_child->parent = d->tree;
				d->tree->second_child = internal_sibling->second_child;
				internal_sibling->second_child->parent = d->tree;
			} else {
				d->tree->second_child = internal_sibling->first_child;
				internal_sibling->first_child->parent = d->tree;
				d->tree->first_child = internal_sibling->second_child;
				internal_sibling->second_child->parent = d->tree;
			}
		}
		/* resize_subtree(wm->root); */
		resize_subtree(d->tree);
	} else {
		// if IN has a parent
		/*            ...
		 *              \
		 *              IN
		 *       	  /   \
		 *           N     IE
		 *                 / \
		 *               E1   E2
		 */
		if (is_sibling_internal(node)) {
			internal_sibling = get_internal_sibling(node);
		} else {
			log_message(ERROR, "internal node is null");
			return -1;
		}
		if (internal_sibling == NULL) {
			log_message(ERROR, "internal node is null");
			return -1;
		}
		internal_sibling->parent = NULL;
		if (node->parent->first_child == node) {
			node->parent->first_child = internal_sibling->first_child;
			internal_sibling->first_child->parent = node->parent;
			node->parent->second_child = internal_sibling->second_child;
			internal_sibling->second_child->parent = node->parent;
		} else {
			node->parent->second_child = internal_sibling->first_child;
			internal_sibling->first_child->parent = node->parent;
			node->parent->first_child = internal_sibling->second_child;
			internal_sibling->second_child->parent = node->parent;
		}
		resize_subtree(node->parent);
	}

	free(internal_sibling);
	internal_sibling = NULL;
	free(node->client);
	node->client = NULL;
	free(node);
	node = NULL;
	return 0;
}

void delete_floating_node(node_t *node, desktop_t *d) {
	if (node == NULL || node->client == NULL || d == NULL) {
		log_message(ERROR, "node to be deleted is null");
		return;
	}

	assert(node->client->state == FLOATING);

	log_message(DEBUG, "DELETE floating window %d", node->client->window);
	node_t *p = node->parent;
	if (p->first_child == node) {
		p->first_child = NULL;
	} else {
		p->second_child = NULL;
	}
	node->parent = NULL;
	free(node->client);
	node->client = NULL;
	free(node);
	node = NULL;
	assert(p->first_child == NULL);
	assert(p->second_child == NULL);
	log_message(DEBUG, "DELETE floating window sucess");
	d->n_count -= 1;
}

void delete_node(node_t *node, desktop_t *d) {
	if (node == NULL || node->client == NULL || d->tree == NULL) {
		log_message(ERROR, "node to be deleted is null");
		return;
	}

	if (node->node_type == INTERNAL_NODE) {
		log_message(ERROR,
					"node to be deleted is not an external node type: %d",
					node->node_type);
		return;
	}

	if (is_parent_null(node) && node != d->tree) {
		log_message(ERROR, "parent of node is null");
		return;
	}

	// node_t *parent = node->parent;

	// early check if only single node/client is mapped to the screen
	if (node == d->tree) {
		free(node->client);
		node->client = NULL;
		free(node);
		node	= NULL;
		d->tree = NULL;
		d->n_count -= 1;
		return;
	}

	/*
	 * case 1: node's parent has two external children
	 *         I
	 *    	 /   \
	 *   	E     E
	 */
	// if (has_external_children(parent)) {
	if (is_sibling_external(node)) {
		xcb_window_t w = node->client->window;
		if (delete_node_with_external_sibling(node) != 0) {
			log_message(ERROR, "cannot delete node with id: %d", w);
			return;
		}
		d->n_count -= 1;
		return;
	}

	/*
	 * case 2: node's parent has one internal child
	 * in other words, node has an internal sibling
	 *         I
	 *    	 /   \
	 *   	E     I
	 *             \
	 *            rest...
	 */
	if (is_sibling_internal(node)) {
		xcb_window_t w = node->client->window;
		if (delete_node_with_internal_sibling(node, d) != 0) {
			log_message(ERROR, "cannot delete node with id: %d", w);
			return;
		}
		d->n_count -= 1;
	}
}

bool has_first_child(const node_t *parent) {
	return parent->first_child != NULL;
}

bool has_second_child(const node_t *parent) {
	return parent->second_child != NULL;
}

bool is_internal(const node_t *node) {
	return node->node_type == INTERNAL_NODE;
}

bool is_external(const node_t *node) {
	return node->node_type == EXTERNAL_NODE;
}

bool is_tree_empty(const node_t *root) {
	return root == NULL;
}

bool is_parent_null(const node_t *node) {
	return node->parent == NULL;
}

bool is_parent_internal(const node_t *node) {
	return node->parent->node_type == INTERNAL_NODE;
}

void log_tree_nodes(node_t *node) {
	if (node != NULL) {
		if (node->client != NULL) {
			xcb_icccm_get_text_property_reply_t t_reply;
			xcb_get_property_cookie_t			cn =
				xcb_icccm_get_wm_name(wm->connection, node->client->window);
			uint8_t wr =
				xcb_icccm_get_wm_name_reply(wm->connection, cn, &t_reply, NULL);
			char name[256];
			if (wr == 1) {
				snprintf(name, sizeof(name), "%s", t_reply.name);
				xcb_icccm_get_text_property_reply_wipe(&t_reply);
			}
			log_message(DEBUG,
						"Node Type: %d, Client Window ID: %u, name: %s",
						node->node_type,
						node->client->window,
						name);
		} else {
			log_message(DEBUG, "Node Type: %d", node->node_type);
		}
		log_tree_nodes(node->first_child);
		log_tree_nodes(node->second_child);
	}
}

int hide_windows(node_t *cn) {
	if (cn == NULL) {
		return 0;
	}

	if (cn->node_type != INTERNAL_NODE && cn->client != NULL) {
		if (hide_window(cn->client->window) != 0) {
			return -1;
		}
	}

	hide_windows(cn->first_child);
	hide_windows(cn->second_child);

	return 0;
}

int show_windows(node_t *cn) {
	if (cn == NULL) {
		return 0;
	}

	if (cn->node_type != INTERNAL_NODE && cn->client != NULL) {
		if (show_window(cn->client->window) != 0) {
			return -1;
		}
	}

	show_windows(cn->first_child);
	show_windows(cn->second_child);

	return 0;
}

bool client_exist(node_t *cn, xcb_window_t win) {
	if (cn == NULL) {
		return false;
	}

	if (cn->client != NULL) {
		if (cn->client->window == win) {
			return true;
		}
	}

	if (client_exist(cn->first_child, win)) {
		return true;
	}

	if (client_exist(cn->second_child, win)) {
		return true;
	}

	return false;
}

bool in_left_subtree(node_t *lc, node_t *n) {
	if (lc == NULL) {
		return false;
	}

	if (lc == n || lc->first_child == n || lc->second_child == n) {
		return true;
	}

	if (in_left_subtree(lc->first_child, n)) {
		return true;
	}

	if (in_left_subtree(lc->second_child, n)) {
		return true;
	}

	return false;
}

bool in_right_subtree(node_t *rc, node_t *n) {
	if (rc == NULL) {
		return false;
	}

	if (rc == n || rc->first_child == n || rc->second_child == n) {
		return true;
	}

	if (in_right_subtree(rc->first_child, n)) {
		return true;
	}

	if (in_right_subtree(rc->second_child, n)) {
		return true;
	}

	return false;
}

int horizontal_resize_wrapper(arg_t *arg) {

	const int i = get_focused_desktop_idx();
	if (i == -1) {
		return -1;
	}
	node_t *root = wm->desktops[i]->tree;
	if (root == NULL) {
		return -1;
	}
	xcb_window_t w = get_window_under_cursor(wm->connection, wm->root_window);
	node_t		*n = find_node_by_window_id(root, w);
	if (n == NULL) {
		return -1;
	}

	horizontal_resize(n, arg->r);
	render_tree(root);
	return 0;
}

void horizontal_resize(node_t *n, resize_t t) {
	const int16_t px			   = 5;
	direction_t	  grow_direction   = NONE;
	direction_t	  shrink_direction = NONE;

	node_t		 *root			   = find_tree_root(n);
	assert(root->node_type == ROOT_NODE);

	if (in_left_subtree(root->first_child, n)) {
		grow_direction	 = RIGHT;
		shrink_direction = LEFT;
	} else if (in_right_subtree(root->second_child, n)) {
		grow_direction	 = LEFT;
		shrink_direction = RIGHT;
	}

	log_message(INFO, "dir == %d", grow_direction);

	if (n->parent == NULL || n->node_type == ROOT_NODE) {
		return;
	}

	/*
	 * case 1: node's parent is the root.
	 * I = Internal node
	 * E = external node
	 *
	 * I's type is ROOT_NODE
	 * Node to delete is E.
	 * logic: grow E's width by 5 pixels
	 * then move E's sibling x by 5 pixels and shrink its width by 5 pixels
	 * E's sibling can be External or Internal -> if internal, resize its
	 * children
	 *         I
	 *    	 /   \
	 *   	E     E/I
	 *             \
	 *            rest...
	 */
	if (n->parent->node_type == ROOT_NODE) {
		node_t *s = NULL;
		if (is_sibling_external(n)) {
			s = get_external_sibling(n);
			if (s == NULL) {
				return;
			}
			if (t == GROW) {
				n->rectangle.width += px;
				s->rectangle.width -= px;
				grow_direction == RIGHT ? (s->rectangle.x += px)
										: (n->rectangle.x -= px);
				// return;
			} else {
				// shrink
				n->rectangle.width -= px;
				s->rectangle.width += px;
				(shrink_direction == LEFT) ? (s->rectangle.x -= px)
										   : (n->rectangle.x += px);
			}
			// return;
		} else {
			// sibling is internal
			s = get_internal_sibling(n);
			if (s == NULL) {
				log_message(ERROR, "internal sibling is null");
				return;
			}
			if (t == GROW) {
				n->rectangle.width += px;
				s->rectangle.width -= px;
				grow_direction == RIGHT ? (s->rectangle.x += px)
										: (n->rectangle.x -= px);
				resize_subtree(s);
				// return;
			} else {
				n->rectangle.width -= px;
				s->rectangle.width += px;
				(shrink_direction == LEFT) ? (s->rectangle.x -= px)
										   : (n->rectangle.x += px);
				resize_subtree(s);
			}
			// return;
		}
		// return;
	}
	/*
	 * case 2: node's parent is not the root (it is INTERNAL_NODE).
	 * I = Internal node
	 * E = external node
	 *
	 * I's type is INTERNAL_NODE
	 * Node to expand is E.
	 * logic: grow the whole subtree(rectangle)'s width in E's side by 5 pixels
	 * then move the opposite subtree(rectangle)'s x by 5 pixels and shrink its
	 * width by 5 pixels
	 *         I
	 *    	 /   \
	 *   	I     E/I
	 *    /   \     \
	 *   E    E/I   rest...
	 */
	if (n->parent->node_type == INTERNAL_NODE) {
		if (in_left_subtree(root->first_child, n)) {
			if (root->first_child == NULL ||
				root->first_child->node_type != INTERNAL_NODE) {
				return;
			}
			(t == GROW) ? (root->first_child->rectangle.width += px)
						: (root->first_child->rectangle.width -= px);
			(t == GROW) ? (root->second_child->rectangle.width -= px)
						: (root->second_child->rectangle.width += px);
			(t == GROW) ? (root->second_child->rectangle.x += px)
						: (root->second_child->rectangle.x -= px);

			if (root->second_child != NULL &&
				root->second_child->node_type == EXTERNAL_NODE) {
				resize_subtree(root->first_child);
				// return;
			} else {
				resize_subtree(root->first_child);
				resize_subtree(root->second_child);
				// return;
			}
		} else {
			if (root->second_child == NULL ||
				root->second_child->node_type != INTERNAL_NODE) {
				return;
			}
			(t == GROW) ? (root->second_child->rectangle.width += px)
						: (root->second_child->rectangle.width -= px);
			(t == GROW) ? (root->second_child->rectangle.x -= px)
						: (root->second_child->rectangle.x += px);
			(t == GROW) ? (root->first_child->rectangle.width -= px)
						: (root->first_child->rectangle.width += px);

			if (root->first_child != NULL &&
				root->first_child->node_type == EXTERNAL_NODE) {
				resize_subtree(root->second_child);
				// return;
			} else {
				resize_subtree(root->first_child);
				resize_subtree(root->second_child);
				// return;
			}
		}
	}
}

node_t *find_left_leaf(node_t *root) {
	if (root == NULL) {
		return NULL;
	}

	if ((root->node_type == EXTERNAL_NODE || root->parent == NULL) &&
		root->client != NULL) {
		return root;
	}

	node_t *left_leaf = find_left_leaf(root->first_child);

	if ((left_leaf != NULL && left_leaf->client != NULL) &&
		(left_leaf->node_type == EXTERNAL_NODE ||
		 left_leaf->node_type == ROOT_NODE)) {
		return left_leaf;
	}

	return find_left_leaf(root->second_child);
}

void unlink_node(node_t *node, desktop_t *d) {
	if (node == NULL) {
		return;
	}

	if (d->tree == node) {
		d->tree = NULL;
		return;
	}

	if (is_sibling_external(node)) {
		node_t *e = get_external_sibling(node);
		if (e == NULL) {
			return;
		}
		if (node->parent->node_type == ROOT_NODE) {
			node->parent->node_type	   = ROOT_NODE;
			node->parent->client	   = e->client;
			node->parent->first_child  = NULL;
			node->parent->second_child = NULL;
		} else {
			node_t *g = node->parent->parent;
			if (g->first_child == node->parent) {
				g->first_child->node_type	 = EXTERNAL_NODE;
				g->first_child->client		 = e->client;
				g->first_child->first_child	 = NULL;
				g->first_child->second_child = NULL;
			} else {
				g->second_child->node_type	  = EXTERNAL_NODE;
				g->second_child->client		  = e->client;
				g->second_child->first_child  = NULL;
				g->second_child->second_child = NULL;
			}
		}
		free(e);
		e			 = NULL;
		node->parent = NULL;
		return;
	} else if (is_sibling_internal(node)) {
		node_t *n = NULL;
		if (node->parent->node_type == ROOT_NODE) {
			n = get_internal_sibling(node);
			if (n == NULL) {
				log_message(ERROR, "internal node is null");
				return;
			}
			if (d->tree == node->parent) {
				d->tree->first_child	= n->first_child;
				n->first_child->parent	= d->tree;
				d->tree->second_child	= n->second_child;
				n->second_child->parent = d->tree;
			} else {
				d->tree->second_child	= n->first_child;
				n->first_child->parent	= d->tree;
				d->tree->first_child	= n->second_child;
				n->second_child->parent = d->tree;
			}
			resize_subtree(d->tree);
		} else {
			if (is_sibling_internal(node)) {
				n = get_internal_sibling(node);
			} else {
				log_message(ERROR, "internal node is null");
				return;
			}
			if (n == NULL) {
				return;
			}
			n->parent = NULL;
			if (node->parent->first_child == node) {
				node->parent->first_child  = n->first_child;
				n->first_child->parent	   = node->parent;
				node->parent->second_child = n->second_child;
				n->second_child->parent	   = node->parent;
			} else {
				node->parent->second_child = n->first_child;
				n->first_child->parent	   = node->parent;
				node->parent->first_child  = n->second_child;
				n->second_child->parent	   = node->parent;
			}
			resize_subtree(node->parent);
		}
		n->parent		= NULL;
		n->first_child	= NULL;
		n->second_child = NULL;
		node->parent	= NULL;
		free(n);
		n = NULL;
	}
}

int transfer_node_wrapper(arg_t *arg) {
	xcb_window_t w = get_window_under_cursor(wm->connection, wm->root_window);
	if (w == wm->root_window) {
		return -1;
	}

	int d = get_focused_desktop_idx();
	if (d == -1) {
		return -1;
	}

	const int i	   = arg->idx;
	node_t	 *root = wm->desktops[d]->tree;
	node_t	 *node = find_node_by_window_id(root, w);

	if (d == i) {
		return 0;
	}

	desktop_t *nd = wm->desktops[i];
	desktop_t *od = wm->desktops[d];
	log_message(DEBUG, "new desktop = %d, old desktop = %d", i + 1, d + 1);
	if (hide_window(node->client->window) != 0) {
		return -1;
	}
	unlink_node(node, od);
	transfer_node(node, nd);

	od->n_count--;
	nd->n_count++;

	if (render_tree(od->tree) != 0) {
		return -1;
	}
	return 0;
}

void transfer_node(node_t *node, desktop_t *d) {
	if (node == NULL || d == NULL) {
		return;
	}

	if (node->client == NULL) {
		return;
	}

	assert(node->parent == NULL);

	if (is_tree_empty(d->tree)) {
		rectangle_t r = {0};
		uint16_t	w = wm->screen->width_in_pixels;
		uint16_t	h = wm->screen->height_in_pixels;
		if (wm->bar != NULL) {
			xcb_get_geometry_reply_t *g =
				get_geometry(wm->bar->window, wm->connection);
			r.x		 = W_GAP;
			r.y		 = (int16_t)(g->height + W_GAP);
			r.width	 = (uint16_t)(w - 2 * W_GAP);
			r.height = (uint16_t)(h - 2 * W_GAP - g->height);
			free(g);
			g = NULL;
		} else {
			r.x		 = W_GAP;
			r.y		 = W_GAP;
			r.width	 = w - W_GAP;
			r.height = h - W_GAP;
		}
		node->node_type	   = ROOT_NODE;
		d->tree			   = node;
		d->tree->rectangle = r;
		d->tree->node_type = ROOT_NODE;
	} else if (d->tree->first_child == NULL && d->tree->second_child == NULL) {
		client_t *c = d->tree->client;
		//		d->tree->first_child			 = create_node(c);
		if ((d->tree->first_child = create_node(c)) == NULL) {
			return;
		}
		d->tree->first_child->node_type	 = EXTERNAL_NODE;
		d->tree->second_child			 = node;
		d->tree->second_child->node_type = EXTERNAL_NODE;
		d->tree->client					 = NULL;
		d->tree->first_child->parent = d->tree->second_child->parent = d->tree;
		resize_subtree(d->tree);
	} else {
		node_t *leaf = find_left_leaf(d->tree);
		if (leaf == NULL) {
			return;
		}
		if (leaf->node_type != ROOT_NODE) {
			leaf->node_type = INTERNAL_NODE;
		}
		leaf->first_child = create_node(leaf->client);
		if (leaf->first_child == NULL) {
			return;
		}
		leaf->first_child->parent	 = leaf;
		leaf->first_child->node_type = EXTERNAL_NODE;
		leaf->client				 = NULL;

		leaf->second_child			 = node;
		if (leaf->second_child == NULL) {
			return;
		}
		leaf->second_child->parent	  = leaf;
		leaf->second_child->node_type = EXTERNAL_NODE;
		resize_subtree(leaf);
	}
}

void fill_stack(node_t *root, client_t *arr, size_t *index) {
	if (root == NULL) {
		return;
	}

	if (root->client != NULL && root->client->window != XCB_NONE) {
		arr[*index] = *(root->client);
		(*index)++;
	}

	fill_stack(root->first_child, arr, index);
	fill_stack(root->second_child, arr, index);
}

client_t *decompose_tree_to_stack(desktop_t *d) {
	if (d->stack == NULL) {
		d->stack = (client_t *)malloc(sizeof(client_t) * d->n_count);
		if (d->stack == NULL) {
			return NULL;
		}
	}
	size_t	i	 = 0;
	node_t *root = d->tree;
	fill_stack(root, d->stack, &i);

	return d->stack;
}